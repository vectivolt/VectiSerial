// ---------------------------------------------------------------------------
// JouleSuite for ESP32 — JouleOTA · JouleSerial · JouleNet · JouleDash
// Author: Chinmoy Bhuyan
// Email:  dikibhuyan@gmail.com
// (c) 2026 — MIT License
// ---------------------------------------------------------------------------

// JouleSerial implementation.
//
// THREADING — the thing to get right here. Two tasks touch this object:
//
//   * the Arduino loop task, via log()/print()/dbg() and JouleSerial::loop();
//   * the AsyncTCP task, via the AsyncWebSocket event handler — which also
//     reaches _log(), because the documented contract lets onMessage()
//     handlers log, and the fragmented-frame path logs an error itself.
//
// So _log() must be callable from either task and must not block on either.
// It therefore does exactly one thing: append to the ring under _mtx. The
// two slow parts — the hardware-Serial mirror and the WebSocket broadcast —
// are done by _pump(), which runs on the loop task only, from
// JouleSerial::loop(). That buys two properties:
//
//   1. nothing blocking (Serial.printf on a stalled CDC/UART, textAll on a
//      full client queue) ever executes on the AsyncTCP task, and
//   2. frames leave in seq order, because one task emits them. When seq was
//      taken under the lock but broadcast outside it, two tasks could invert
//      a pair, and the UI's monotonic-seq dedupe drops the loser for good.
//
// The ring is the queue: _sentSeq marks how far _pump() has got. Nothing is
// copied twice, and a line that falls off the ring before _pump() sees it is
// simply gone — the same fate it would have had in the ring.
//
// That dedupe is why seq also has to survive a reboot. It is
// (bootId << 32) + line, with bootId an NVS counter bumped in begin() — see
// nextBootId(). A counter that restarted at 1 every boot left every open tab
// discarding the log forever, because their high-water mark does not reset.
//
// Inbound frames carry user-typed commands: `{type:"cmd", text:"..."}`.
// We dispatch them to the onMessage() callback exactly as typed (no
// trimming, no level prefix) so the host sketch can implement whatever
// command language it likes.

#include "JouleSerial.h"
#include "JouleSerial_ui_gz.h"
#include <Preferences.h>
#include <stdarg.h>

// Serve pre-compressed UI with Content-Encoding: gzip. Browsers inflate
// transparently. 66 KB → 24 KB on the wire, which makes the page reachable
// on weak Wi-Fi links where the uncompressed version stalls.
static void sendGzippedUi(AsyncWebServerRequest *req, const uint8_t *gz, size_t len) {
  AsyncWebServerResponse *res = req->beginResponse(200, "text/html; charset=utf-8", gz, len);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("Cache-Control", "public, max-age=3600");
  req->send(res);
}

namespace joule {

JouleSerialClass::JouleSerialClass() {}

// Reads the string value of `key` (quoted, e.g. "\"text\"") out of a flat JSON
// object. Returns the index just past the value's closing quote, or -1 if the
// key is missing or its value is not a string.
//
// Written by hand so the library carries no runtime JSON dependency; the
// inbound frame is two fixed keys. Two things a naive scan gets wrong:
//
//   * backslashes — `indexOf('"')` stops on the \" inside `set name \"foo\"`
//     and silently hands the sketch half a command, so the value is walked
//     byte by byte instead;
//   * non-string values — `indexOf('"', afterKey)` happily skips over the 0
//     in {"type":0,"cmd":1,...} and latches onto the *next key's* opening
//     quote, so {"type":0,...} would read back as type=="cmd". The value is
//     anchored to the key's own colon instead: whitespace, one colon, then a
//     quote or nothing.
//
// The scan resumes past a match that is NOT followed by a colon, because the
// same bytes appear in values too: the command `type` arrives as
// {"text":"type","type":"cmd"}, where the first hit for "\"type\"" is the
// value at offset 8. Stopping there dropped the command silently.
static int jsonStringValue(const String &s, const char *key, String &out) {
  const int klen = (int)strlen(key);
  for (int k = s.indexOf(key); k >= 0; k = s.indexOf(key, k + 1)) {
    int q1 = k + klen;
    while (q1 < (int)s.length() && isspace((unsigned char)s[q1])) q1++;
    if (q1 >= (int)s.length() || s[q1] != ':') continue;   // a value, not a key
    q1++;
    while (q1 < (int)s.length() && isspace((unsigned char)s[q1])) q1++;
    if (q1 >= (int)s.length() || s[q1] != '"') return -1;  // present, not a string
    out = "";
    for (int i = q1 + 1; i < (int)s.length(); i++) {
      char c = s[i];
      if (c == '"') return i + 1;
      if (c != '\\') { out += c; continue; }
      if (++i >= (int)s.length()) return -1;   // trailing backslash: malformed
      switch (s[i]) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          // JSON.stringify only emits \u for control bytes, so consuming the
          // four hex digits matters more than decoding them — left in place
          // they would leak into the command as literal text.
          if (i + 4 < (int)s.length()) {
            long cp = strtol(s.substring(i + 1, i + 5).c_str(), nullptr, 16);
            i += 4;
            if (cp > 0 && cp < 0x80) out += (char)cp;
          }
          break;
        }
        default: out += s[i];                  // \" \\ \/ and anything else
      }
    }
    return -1;                                 // unterminated string
  }
  return -1;                                   // key absent
}

static String escapeJson(const String &s) {
  String out; out.reserve(s.length() + 8);
  for (size_t i=0;i<s.length();i++) {
    char c = s[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\':out += "\\\\"; break;
      case '\n':out += "\\n"; break;
      case '\r':out += "\\r"; break;
      case '\t':out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) { char buf[8]; snprintf(buf,8,"\\u%04x",c); out += buf; }
        else out += c;
    }
  }
  return out;
}

// Boot counter, bumped once per boot and kept in NVS. This is what makes `seq`
// monotonic across a restart: without it a rebooted device restarts seq at 1,
// every tab that was open holds a much higher high-water mark from before the
// reboot, and the reconnected console silently discards the entire log — both
// the replayed history and every live line after it — until the device happens
// to out-count its previous run. The console looks connected and is dead.
//
// One NVS write per boot. NVS wear-levels its pages, and this is one 4-byte
// entry; a device that reboots every minute for a year is well inside spec.
// If NVS is unavailable the counter stays 0 and behaviour degrades to exactly
// what it was before: correct within a boot, stale-client-hostile across one.
//
// ponytail: a plain counter, not a random nonce — it has to *increase*, and
// nothing else on the chip survives a power cycle.
static uint32_t nextBootId() {
  Preferences p;
  if (!p.begin("joule-serial", false)) return 0;
  uint32_t id = p.getUInt("boot", 0) + 1;
  p.putUInt("boot", id);
  p.end();
  return id;
}

void JouleSerialClass::begin(AsyncWebServer *server, const String &username, const String &password) {
  _user = username; _pass = password;

  // A second begin() is a credentials change, not a second mount: re-running
  // the registrations would leak the old AsyncWebSocket and leave two
  // handlers matching /serial/ws, with the server dispatching to the stale
  // one while _log() broadcasts on the new one.
  if (_ws) { _applyWsAuth(); return; }
  _server = server;

  // Claim this boot's seq range. Done here, not in the constructor: NVS is not
  // initialised when static constructors run. Lines logged before begin() keep
  // the low seqs they already have — still ascending, just below the range.
  // Only ever moved forward: with no usable NVS the base is 0, and rewinding
  // to it would hand a second line the seq an earlier one already used.
  _bootId = nextBootId();
  {
    asyncsrv::lock_guard_type g(_mtx);
    uint64_t base = (uint64_t)_bootId << 32;
    if (base > _seq) _seq = base;
  }

  // Use exact() match so `/serial` doesn't swallow `/serial/ws` (the
  // WebSocket route is registered through addHandler() and matches first
  // anyway, but explicit-exact prevents future sub-path collisions).
  _server->on(AsyncURIMatcher::exact("/serial"), HTTP_GET, [this](AsyncWebServerRequest *req){
    if (_user.length() && !req->authenticate(_user.c_str(), _pass.c_str())) return req->requestAuthentication();
    sendGzippedUi(req, joule::SERIAL_UI_HTML_GZ, joule::SERIAL_UI_HTML_GZ_LEN);
  });

  _attachWs();
}

// AsyncWebSocket is an AsyncWebHandler, so the auth middleware runs on the
// upgrade request before the handshake completes. Without this the console
// password only guards the HTML: anyone who can reach the device opens
// /serial/ws directly, gets the whole log replayed, and can send commands.
// Same realm and default scheme as the /serial challenge, so the browser
// reuses the credentials it already cached for the page.
void JouleSerialClass::_applyWsAuth() {
  if (!_ws) return;
  _ws->setAuthentication(_user, _pass);
}

void JouleSerialClass::_attachWs() {
  _ws = new AsyncWebSocket("/serial/ws");
  _applyWsAuth();
  _ws->onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                       AwsEventType type, void *arg, uint8_t *data, size_t len){
    switch (type) {
      case WS_EVT_CONNECT: {
        // Replay history so the new tab isn't blank. A client that already
        // has part of the log can reconnect to /serial/ws?since=<last seq>
        // and get only what it missed instead of a duplicate copy.
        AsyncWebServerRequest *req = (AsyncWebServerRequest *)arg;
        uint64_t since = 0;
        // strtoull, not String::toInt(): seq carries the boot id in its high
        // 32 bits, so it does not fit a long.
        if (req && req->hasParam("since"))
          since = strtoull(req->getParam("since")->value().c_str(), nullptr, 10);
        _sendHistory(client, since);
        // Notify everyone of the new client count.
        String n = String("{\"type\":\"clients\",\"n\":") + server->count() + "}";
        server->textAll(n);
        break;
      }
      case WS_EVT_DISCONNECT: {
        String n = String("{\"type\":\"clients\",\"n\":") + server->count() + "}";
        server->textAll(n);
        break;
      }
      case WS_EVT_DATA: {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->opcode != WS_TEXT) break;
        if (!(info->final && info->index == 0 && info->len == len)) {
          // There is no reassembly buffer: a command long enough to fragment
          // is a paste accident, not a command. Say so once per message —
          // silently dropping it makes the device look hung.
          // ponytail: single-frame commands only; add a per-client
          // accumulator with a hard cap if long payloads ever matter.
          if (info->index == 0) _log(LogLevel::Error, "command dropped — too long for one WebSocket frame");
          break;
        }
        String s; s.reserve(len); for (size_t i=0;i<len;i++) s += (char)data[i];
        // The type discriminator is checked, not assumed: _onMessage lands in
        // the host sketch's command table, so any frame that merely happens
        // to carry a "text" key must not be able to run commands.
        //
        // Both keys are searched from index 0, so key order is not load-
        // bearing: {"text":...,"type":"cmd"} is as valid as the UI's own
        // order, which matters because README invites third-party clients and
        // not every serialiser preserves insertion order.
        String kind, cmd;
        if (jsonStringValue(s, "\"type\"", kind) < 0 || kind != "cmd") break;
        if (jsonStringValue(s, "\"text\"", cmd) < 0) break;
        if (_onMessage) _onMessage(cmd);
        break;
      }
      default: break;
    }
  });
  _server->addHandler(_ws);
}

// Called from BOTH tasks (loop and AsyncTCP — see the threading note at the
// top). Ring append only: no allocation-heavy encode, no Serial, no socket
// write. Everything slow is _pump()'s job, on the loop task.
void JouleSerialClass::_log(LogLevel lvl, const String &line) {
  asyncsrv::lock_guard_type g(_mtx);
  uint32_t now = millis();
  if (now < _lastMs) _msHigh += 0x100000000ULL;   // millis() rolled over
  _lastMs = now;
  _history.push_back(Line{ ++_seq, _msHigh + now, (uint8_t)lvl, line });
  while (_history.size() > _historyMax) _history.pop_front();
}

// LOOP TASK ONLY. Drains the ring's un-emitted tail to Serial and to every
// connected client, in seq order. Both sinks block — Serial.printf stalls on a
// full UART/CDC TX buffer, textAll walks the client list — which is exactly
// why neither may run on the AsyncTCP task.
void JouleSerialClass::_pump() {
  for (;;) {
    // The line is COPIED out under the lock and released before either sink
    // runs. Holding _mtx across textAll() would deadlock: _newClient holds the
    // socket's client lock and then takes _mtx via _sendHistory, so the two
    // orders are an ABBA. And referencing the ring's String without the lock
    // is a use-after-free — the next _log() may pop_front() it away.
    uint64_t seq; uint64_t ms; uint8_t lvl; String text;
    {
      asyncsrv::lock_guard_type g(_mtx);
      if (_history.empty() || _history.back().seq <= _sentSeq) return;
      const Line *next = nullptr;
      for (const auto &l : _history) if (l.seq > _sentSeq) { next = &l; break; }
      if (!next) return;
      seq = next->seq; ms = next->ms; lvl = next->lvl; text = next->text;
      _sentSeq = seq;
    }

    if (_mirrorHw) {
      static const char *LV[] = {"DBG","INF","WRN","ERR"};
      Serial.printf("[%llu] %s %s\n", (unsigned long long)ms, LV[lvl], text.c_str());
    }

    if (_ws && _ws->count() > 0) {
      String j; j.reserve(text.length() + 64);
      j  = "{\"type\":\"line\",\"seq\":"; j += seq;
      j += ",\"ms\":";                   j += ms;
      j += ",\"lvl\":";                  j += (int)lvl;
      j += ",\"text\":\"";               j += escapeJson(text);
      j += "\"}";
      _ws->textAll(j);
    }
  }
}

// Lines per `hist` frame. One frame for the whole ring does not scale: at the
// setHistorySize(1024) the examples use, that is a ~75 KB String built by
// thousands of `+=` reallocations and then copied again inside
// AsyncWebSocketClient::text() — two contiguous 75 KB blocks on a heap that
// has been fragmenting since boot. 64 lines is ~5 KB a frame, and even a
// 2000-line ring stays inside the per-client send queue (32 messages).
static const size_t kHistChunkLines = 64;

// AsyncTCP TASK (WS_EVT_CONNECT). Replays the ring to one client as a series
// of `hist` frames; the UI ingests each frame's lines independently, so more
// than one is fine.
void JouleSerialClass::_sendHistory(AsyncWebSocketClient *client, uint64_t sinceSeq) {
  uint64_t cursor = sinceSeq;
  size_t n = 0;
  do {
    // _historyJson takes and releases _mtx itself; client->text() must run
    // with it released (ABBA — see _pump).
    client->text(_historyJson(cursor, cursor, n));
  } while (n == kHistChunkLines);
}

// Builds one chunk: up to kHistChunkLines lines with seq > sinceSeq. `cursor`
// is set to the seq of the last line included, `count` to how many.
String JouleSerialClass::_historyJson(uint64_t sinceSeq, uint64_t &cursor, size_t &count) const {
  String j; j.reserve(kHistChunkLines * 80 + 128);
  count = 0;
  {
    // Held across the whole build, not just the walk: the ring's Strings can be
    // pop_front()ed by _log() on the other task, and _title / _brandColor can
    // be replaced by a setTitle() from loop() while we read them here. Bounded
    // to one chunk, so the hold is short and the caller resumes from `cursor`
    // on the next frame. Nothing blocking runs inside — client->text() is the
    // caller's job, with the lock released.
    asyncsrv::lock_guard_type g(_mtx);

    // title + brand let the browser apply setTitle() / setBrandColor() at
    // runtime even though the gzipped HTML body has no template subst path.
    // boot identifies this power-up: a client that sees it change knows the
    // device restarted and can drop any state keyed on the old seq range.
    j  = "{\"type\":\"hist\",\"boot\":";
    j += _bootId; j += ",\"title\":\"";
    j += escapeJson(_title); j += "\",\"brand\":\"";
    j += escapeJson(_brandColor); j += "\",\"lines\":[";

    for (const auto &l : _history) {
      if (l.seq <= sinceSeq) continue;
      if (count) j += ',';
      j += "{\"seq\":"; j += l.seq;
      j += ",\"ms\":"; j += l.ms;
      j += ",\"lvl\":"; j += (int)l.lvl;
      j += ",\"text\":\""; j += escapeJson(l.text); j += "\"}";
      cursor = l.seq;
      if (++count == kHistChunkLines) break;
    }
  }
  j += "]}";
  return j;
}

// LOOP TASK. Reaps closed clients and emits everything _log() has queued.
void JouleSerialClass::loop() {
  if (_ws) _ws->cleanupClients();
  _pump();
}

// ----- Print interface -----------------------------------------------------

// Cap on the Print accumulator. A stream that never sends a line terminator
// (a CR-only GPS, a print() of a long blob) would otherwise grow this String
// at the full data rate until the heap is gone, and none of it would ever
// reach the console.
static const size_t kMaxWriteLine = 512;

size_t JouleSerialClass::write(uint8_t c) {
  // print() is documented as callable from either task, so the accumulator is
  // shared state: two tasks doing `_writeBuf += c` will eventually have one of
  // them realloc the buffer the other is writing through. _mtx is recursive,
  // so holding it across _log() below is fine, and _log() does nothing that
  // blocks.
  asyncsrv::lock_guard_type g(_mtx);
  if (c == '\r' || c == '\n') {
    // CR alone terminates a line too, so CR-only peripherals flush; the
    // paired LF of a CRLF is swallowed instead of logging a blank line.
    if (!(c == '\n' && _lastWasCR)) { _log(LogLevel::Info, _writeBuf); _writeBuf = ""; }
    _lastWasCR = (c == '\r');
    return 1;
  }
  _lastWasCR = false;
  _writeBuf += (char)c;
  if (_writeBuf.length() >= kMaxWriteLine) { _log(LogLevel::Info, _writeBuf); _writeBuf = ""; }
  return 1;
}

size_t JouleSerialClass::write(const uint8_t *buf, size_t size) {
  // One lock for the block rather than one per byte, and it keeps a multi-byte
  // print() from interleaving with the other task mid-line.
  asyncsrv::lock_guard_type g(_mtx);
  for (size_t i=0;i<size;i++) write(buf[i]);
  return size;
}

// ----- printf helpers ------------------------------------------------------

// Ceiling on the heap re-format. Past this a log line is a data dump, and
// allocating megabytes to print it is a worse failure than clipping it.
static const int kMaxFmtLine = 2048;

void JouleSerialClass::_logv(LogLevel lvl, const char *fmt, va_list ap) {
  // vsnprintf consumes ap, so the overflow path needs its own copy — the
  // classic bug here is "growing" the buffer and then concatenating the
  // already-truncated stack copy into it, which allocates the space and
  // still logs 159 characters.
  va_list ap2;
  va_copy(ap2, ap);
  char stack[160];
  int n = vsnprintf(stack, sizeof(stack), fmt, ap);
  if (n < 0) { va_end(ap2); _log(lvl, "<bad format string>"); return; }
  if (n < (int)sizeof(stack)) { va_end(ap2); _log(lvl, String(stack)); return; }

  int want = n < kMaxFmtLine ? n : kMaxFmtLine;
  char *heap = (char *)malloc(want + 1);
  if (!heap) {
    va_end(ap2);
    _log(lvl, String(stack) + "\xE2\x80\xA6");   // out of heap: keep what fits
    return;
  }
  vsnprintf(heap, want + 1, fmt, ap2);
  va_end(ap2);
  String out(heap);
  free(heap);
  if (n > want) out += "\xE2\x80\xA6";           // U+2026, so a clip is visible
  _log(lvl, out);
}

#define JS_FMT_IMPL(NAME, LVL) \
  void JouleSerialClass::NAME(const char *fmt, ...) { \
    va_list ap; va_start(ap, fmt); _logv(LVL, fmt, ap); va_end(ap); \
  }
JS_FMT_IMPL(dbg, LogLevel::Debug)
JS_FMT_IMPL(inf, LogLevel::Info)
JS_FMT_IMPL(wrn, LogLevel::Warn)
JS_FMT_IMPL(err, LogLevel::Error)
#undef JS_FMT_IMPL

} // namespace joule

joule::JouleSerialClass JouleSerial;
