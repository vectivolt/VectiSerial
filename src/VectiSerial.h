// ---------------------------------------------------------------------------
// VectiSuite for ESP32 — VectiOTA · VectiSerial · VectiNet · VectiDash
// Author: VectiVolt team
// (c) 2026 VectiVolt — Apache-2.0 License
// ---------------------------------------------------------------------------

// VectiSerial — wireless serial console over WebSocket.
//
// Why this exists: WebSerial.pro is closed-source and uses polling HTTP, no
// log levels, no search, and the input bar is a single global field.
// VectiSerial does the same in 4 lines but adds:
//
//   * WebSocket transport — sub-100ms round trip even with 10 connected
//     clients, vs. WebSerial's 1s poll cadence.
//   * Log levels (DEBUG / INFO / WARN / ERROR) with ANSI colors rendered
//     in the browser. printf-style formatters: dbg("x=%d", 42).
//   * Per-line timestamps — uptime in ms, monotonic across the 49.7-day
//     millis() rollover — toggled from the UI.
//   * Command history (arrow keys), inline search, hex/ascii view toggle,
//     export to JSON / CSV / plain text.
//   * Multi-client — every connected tab sees the same stream and the same
//     input commands; useful when an engineer and a tech are debugging
//     together remotely.
//   * Built-in line ring buffer (default 256 lines) so a client that
//     connects mid-session sees recent history immediately.
//
// Usage:
//
//   #include <VectiSerial.h>
//   VectiSerial.begin(&server, "admin", "vecti");
//   VectiSerial.info("hello world");
//   VectiSerial.onMessage([](const String &cmd){ Serial.println(cmd); });
#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <functional>
#include <deque>
#include <stdarg.h>

namespace vecti {

enum class LogLevel : uint8_t { Debug=0, Info=1, Warn=2, Error=3 };

using SerialMessageCb = std::function<void(const String &command)>;

class VectiSerialClass : public Print {
public:
  VectiSerialClass();

  // Mount /serial (UI) + /serial/ws (WebSocket). If a username is given both
  // are gated — the WebSocket carries the whole log and accepts commands, so
  // leaving it open would make the password on /serial decorative.
  // Calling begin() again only updates the credentials; routes stay as they
  // were registered the first time.
  void begin(AsyncWebServer *server,
             const String &username = "",
             const String &password = "");

  // Logging entry points — both Print-style and printf-style. The four named
  // levels match what most embedded loggers use; the UI tints each level.
  using Print::print;
  using Print::println;
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buf, size_t size) override;

  void debug(const String &line)   { _log(LogLevel::Debug, line); }
  void info (const String &line)   { _log(LogLevel::Info,  line); }
  void warn (const String &line)   { _log(LogLevel::Warn,  line); }
  void error(const String &line)   { _log(LogLevel::Error, line); }

  // printf-style helpers. Each call is one log entry; results longer than the
  // stack buffer are re-formatted on the heap up to a cap, then marked with
  // an ellipsis so a clipped line never reads as a complete one.
  void dbg(const char *fmt, ...) __attribute__((format(printf,2,3)));
  void inf(const char *fmt, ...) __attribute__((format(printf,2,3)));
  void wrn(const char *fmt, ...) __attribute__((format(printf,2,3)));
  void err(const char *fmt, ...) __attribute__((format(printf,2,3)));

  // Inbound commands typed in the UI's input bar.
  //
  // The callback runs on the AsyncTCP task, not from loop(). Treat it like an
  // ISR-adjacent context: no delay(), no ESP.restart(), no blocking flash or
  // NVS writes — those stall every other socket on the device and can tear
  // down the stack from inside its own callback. Set a flag and act on it in
  // loop(). Logging from here is fine: log() only appends to the ring, and
  // the blocking half (Serial mirror, WebSocket broadcast) happens in loop().
  void onMessage(SerialMessageCb cb) { _onMessage = std::move(cb); }

  // History buffer depth. Older lines drop off the back.
  void setHistorySize(size_t n) { _historyMax = n; }

  // Under _mtx because a connecting client reads both from the AsyncTCP task
  // while a sketch may still be calling these from loop(): String assignment
  // frees the old buffer, so an unguarded read is a use-after-free, not a
  // torn read. Cheap enough — neither is a hot path.
  void setTitle(const String &t){ asyncsrv::lock_guard_type g(_mtx); _title = t; }
  void setBrandColor(const String &css){ asyncsrv::lock_guard_type g(_mtx); _brandColor = css; }

  // If true (default), VectiSerial also mirrors every line to Serial.
  // Disable when you want the wireless console to be the only sink.
  void setMirrorToHardwareSerial(bool on) { _mirrorHw = on; }

  // Call from loop(). Two jobs, both mandatory:
  //   * reaps disconnected AsyncWebSocketClient objects — skip it and every
  //     dropped tab leaks its client struct and message queue;
  //   * emits queued log lines to Serial and to the WebSocket. log() only
  //     appends to the ring; nothing reaches either sink until loop() runs.
  //     That is deliberate — both writes block, and log() is reachable from
  //     the AsyncTCP task. A sketch that stops calling loop() stops logging.
  void loop();

private:
  void _attachWs();
  void _applyWsAuth();
  void _log(LogLevel lvl, const String &line);
  void _logv(LogLevel lvl, const char *fmt, va_list ap);
  void _pump();                                     // loop task only
  void _sendHistory(AsyncWebSocketClient *client, uint64_t sinceSeq);
  String _historyJson(uint64_t sinceSeq, uint64_t &cursor, size_t &count) const;

  AsyncWebServer   *_server = nullptr;
  AsyncWebSocket   *_ws     = nullptr;

  String _user, _pass;
  String _title      = "VectiSerial";
  String _brandColor = "#2ee5a0";
  bool   _mirrorHw   = true;

  struct Line { uint64_t seq; uint64_t ms; uint8_t lvl; String text; };
  std::deque<Line> _history;
  size_t   _historyMax = 256;

  // seq is (bootId << 32) + line-number-within-this-boot, and it must keep
  // rising ACROSS reboots. Clients dedupe on a monotonic seq and hold that
  // high-water mark across the auto-reconnect; a device whose counter restarts
  // at 1 leaves every already-open tab silently discarding the whole log until
  // it out-counts the previous boot. bootId comes from an NVS counter bumped
  // once in begin(); 2^32 lines per boot, and seq stays inside JavaScript's
  // exact-integer range (2^53) for the first ~2 million boots.
  uint64_t _seq        = 0;
  uint64_t _sentSeq    = 0;   // how far _pump() has emitted; loop task + _mtx
  uint32_t _bootId     = 0;   // 0 until begin(), and if NVS is unavailable

  // millis() wraps every 49.7 days; _msHigh carries the overflow so a line
  // logged after the wrap never appears to predate one logged before it.
  uint32_t _lastMs = 0;
  uint64_t _msHigh = 0;

  // Guards _history, _seq, _sentSeq, _title, _brandColor, the Print
  // accumulator below, and the uptime accumulator. Written from
  // BOTH tasks: loop() logs, and so does the AsyncTCP task (onMessage handlers
  // are documented as allowed to log, and the oversized-frame path logs an
  // error). Read from the AsyncTCP task when a client connects. On a dual-core
  // ESP32 those run at the same instant, and an unguarded push_back while
  // _historyJson() walks the deque is a use-after-free, not a torn read.
  //
  // Never held across Serial.printf(), textAll() or client->text(): the socket
  // takes its own client lock before invoking our connect handler, so locking
  // in the other order would be an ABBA deadlock.
  //
  // It is recursive (asyncsrv::mutex_type is std::recursive_mutex), which is
  // what lets write() hold it across its own call to _log().
  mutable asyncsrv::mutex_type _mtx;

  // Print interface accumulator. Shared state like everything else here:
  // print() is documented as callable from either task, and a String growing
  // under two tasks reallocates out from under one of them.
  String   _writeBuf;             // under _mtx
  bool     _lastWasCR = false;    // under _mtx — so CRLF flushes once, not twice

  SerialMessageCb _onMessage;
};

} // namespace vecti

extern vecti::VectiSerialClass VectiSerial;
