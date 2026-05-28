// ---------------------------------------------------------------------------
// JouleSuite for ESP32 / ESP8266 — JouleOTA · JouleSerial · JouleNet · JouleDash
// Author: Chinmoy Bhuyan
// Email:  dikibhuyan@gmail.com
// (c) 2026 — MIT License
// ---------------------------------------------------------------------------

// JouleSerial implementation.
//
// The transport is a single AsyncWebSocket. Each log() call:
//   1. is appended to an in-RAM ring buffer (so a new client can replay
//      the recent past on connect),
//   2. is broadcast as a JSON frame `{type:"line", seq, ms, lvl, text}`,
//   3. is optionally mirrored to the hardware UART so engineers staring
//      at a terminal don't miss anything.
//
// Inbound frames carry user-typed commands: `{type:"cmd", text:"..."}`.
// We dispatch them to the onMessage() callback exactly as typed (no
// trimming, no level prefix) so the host sketch can implement whatever
// command language it likes.

#include "JouleSerial.h"
#include "JouleSerial_ui.h"
#include "JouleSerial_ui_gz.h"
#include <stdarg.h>

// Serve pre-compressed UI with Content-Encoding: gzip. Browsers inflate
// transparently. ~8 KB → ~3.5 KB on the wire, which makes the page
// reachable on weak Wi-Fi links where the uncompressed version stalls.
static void sendGzippedUi(AsyncWebServerRequest *req, const uint8_t *gz, size_t len) {
  AsyncWebServerResponse *res = req->beginResponse(200, "text/html; charset=utf-8", gz, len);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("Cache-Control", "public, max-age=3600");
  req->send(res);
}

namespace joule {

JouleSerialClass::JouleSerialClass() {}

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

static String renderHtml(const String &title, const String &brand) {
  String s = FPSTR(SERIAL_UI_HTML);
  s.replace("__TITLE__", title);
  s.replace("__BRAND__", brand);
  return s;
}

void JouleSerialClass::begin(AsyncWebServer *server, const String &username, const String &password) {
  _server = server; _user = username; _pass = password;

  // Use exact() match so `/serial` doesn't swallow `/serial/ws` (the
  // WebSocket route is registered through addHandler() and matches first
  // anyway, but explicit-exact prevents future sub-path collisions).
  _server->on(AsyncURIMatcher::exact("/serial"), HTTP_GET, [this](AsyncWebServerRequest *req){
    if (_user.length() && !req->authenticate(_user.c_str(), _pass.c_str())) return req->requestAuthentication();
    sendGzippedUi(req, joule::SERIAL_UI_HTML_GZ, joule::SERIAL_UI_HTML_GZ_LEN);
  });

  _attachWs();
}

void JouleSerialClass::_attachWs() {
  _ws = new AsyncWebSocket("/serial/ws");
  _ws->onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                       AwsEventType type, void *arg, uint8_t *data, size_t len){
    switch (type) {
      case WS_EVT_CONNECT: {
        // Replay history so the new tab isn't blank.
        client->text(_historyJson(0));
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
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
          // Parse minimal {"type":"cmd","text":"..."} without ArduinoJson to
          // keep this library standalone of the JSON dep at runtime — this
          // is a 1-key 1-value frame so a 5-line parser is fine.
          String s; s.reserve(len); for (size_t i=0;i<len;i++) s += (char)data[i];
          int t = s.indexOf("\"text\""); if (t < 0) break;
          int q1 = s.indexOf('"', t+6); int q2 = s.indexOf('"', q1+1);
          if (q1<0 || q2<0) break;
          String cmd; cmd.reserve(q2-q1);
          for (int i=q1+1;i<q2;i++) {
            char c = s[i];
            if (c=='\\' && i+1<q2) { char n=s[i+1]; if (n=='n')cmd+='\n';else if(n=='t')cmd+='\t';else if(n=='"')cmd+='"';else if(n=='\\')cmd+='\\';else cmd+=n; i++; }
            else cmd += c;
          }
          if (_onMessage) _onMessage(cmd);
        }
        break;
      }
      default: break;
    }
  });
  _server->addHandler(_ws);
}

void JouleSerialClass::_log(LogLevel lvl, const String &line) {
  // Append to history.
  Line l{ ++_seq, millis(), (uint8_t)lvl, line };
  _history.push_back(l);
  while (_history.size() > _historyMax) _history.pop_front();

  if (_mirrorHw) {
    static const char *LV[] = {"DBG","INF","WRN","ERR"};
    Serial.printf("[%lu] %s %s\n", (unsigned long)l.ms, LV[(int)lvl], line.c_str());
  }

  if (_ws && _ws->count() > 0) {
    String j; j.reserve(line.length() + 64);
    j  = "{\"type\":\"line\",\"seq\":"; j += l.seq;
    j += ",\"ms\":";                   j += l.ms;
    j += ",\"lvl\":";                  j += (int)lvl;
    j += ",\"text\":\"";               j += escapeJson(line);
    j += "\"}";
    _ws->textAll(j);
  }
}

String JouleSerialClass::_historyJson(int /*sinceSeq*/) const {
  // title + brand let the browser apply setTitle() / setBrandColor() at
  // runtime even though the gzipped HTML body has no template subst path.
  String j = "{\"type\":\"hist\",\"title\":\"";
  j += escapeJson(_title); j += "\",\"brand\":\"";
  j += escapeJson(_brandColor); j += "\",\"lines\":[";
  bool first = true;
  for (const auto &l : _history) {
    if (!first) j += ',';
    first = false;
    j += "{\"seq\":"; j += l.seq;
    j += ",\"ms\":"; j += l.ms;
    j += ",\"lvl\":"; j += (int)l.lvl;
    j += ",\"text\":\""; j += escapeJson(l.text); j += "\"}";
  }
  j += "]}";
  return j;
}

// ----- Print interface -----------------------------------------------------

size_t JouleSerialClass::write(uint8_t c) {
  if (c == '\n') { _log(LogLevel::Info, _writeBuf); _writeBuf = ""; }
  else if (c != '\r') _writeBuf += (char)c;
  return 1;
}

size_t JouleSerialClass::write(const uint8_t *buf, size_t size) {
  for (size_t i=0;i<size;i++) write(buf[i]);
  return size;
}

// ----- printf helpers ------------------------------------------------------

#define JS_FMT_IMPL(NAME, LVL) \
  void JouleSerialClass::NAME(const char *fmt, ...) { \
    char stack[160]; va_list ap; va_start(ap, fmt); \
    int n = vsnprintf(stack, sizeof(stack), fmt, ap); va_end(ap); \
    if (n < (int)sizeof(stack)) { _log(LVL, String(stack)); return; } \
    String big; big.reserve(n+1); big.concat(stack); \
    /* truncated — refuse to allocate huge string in heap pressure */ \
    _log(LVL, big); \
  }
JS_FMT_IMPL(dbg, LogLevel::Debug)
JS_FMT_IMPL(inf, LogLevel::Info)
JS_FMT_IMPL(wrn, LogLevel::Warn)
JS_FMT_IMPL(err, LogLevel::Error)
#undef JS_FMT_IMPL

} // namespace joule

joule::JouleSerialClass JouleSerial;
