// ---------------------------------------------------------------------------
// JouleSuite for ESP32 / ESP8266 — JouleOTA · JouleSerial · JouleNet · JouleDash
// Author: Chinmoy Bhuyan
// Email:  dikibhuyan@gmail.com
// (c) 2026 — MIT License
// ---------------------------------------------------------------------------

// JouleSerial — wireless serial console over WebSocket.
//
// Why this exists: WebSerial.pro is closed-source and uses polling HTTP, no
// log levels, no search, and the input bar is a single global field.
// JouleSerial does the same in 4 lines but adds:
//
//   * WebSocket transport — sub-100ms round trip even with 10 connected
//     clients, vs. WebSerial's 1s poll cadence.
//   * Log levels (DEBUG / INFO / WARN / ERROR) with ANSI colors rendered
//     in the browser. printf-style formatters: dbg("x=%d", 42).
//   * Per-line timestamps (relative ms since boot, or absolute if time is
//     synced via NTP) toggled from the UI.
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
//   #include <JouleSerial.h>
//   JouleSerial.begin(&server, "admin", "joule");
//   JouleSerial.info("hello world");
//   JouleSerial.onMessage([](const String &cmd){ Serial.println(cmd); });
#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <functional>
#include <deque>

namespace joule {

enum class LogLevel : uint8_t { Debug=0, Info=1, Warn=2, Error=3 };

using SerialMessageCb = std::function<void(const String &command)>;

class JouleSerialClass : public Print {
public:
  JouleSerialClass();

  // Mount /serial (UI) + /serial/ws (WebSocket) + /serial/api endpoints.
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

  // printf-style helpers. Output is line-buffered: a newline flushes the
  // accumulated buffer as one log entry.
  void dbg(const char *fmt, ...) __attribute__((format(printf,2,3)));
  void inf(const char *fmt, ...) __attribute__((format(printf,2,3)));
  void wrn(const char *fmt, ...) __attribute__((format(printf,2,3)));
  void err(const char *fmt, ...) __attribute__((format(printf,2,3)));

  // Inbound commands typed in the UI's input bar.
  void onMessage(SerialMessageCb cb) { _onMessage = std::move(cb); }

  // History buffer depth. Older lines drop off the back.
  void setHistorySize(size_t n) { _historyMax = n; }
  void setTitle(const String &t){ _title = t; }
  void setBrandColor(const String &css){ _brandColor = css; }

  // If true (default), JouleSerial also mirrors every line to Serial.
  // Disable when you want the wireless console to be the only sink.
  void setMirrorToHardwareSerial(bool on) { _mirrorHw = on; }

  // Loop hook (no-op today — reserved for future ping/keepalive batching).
  void loop() {}

private:
  void _attachWs();
  void _log(LogLevel lvl, const String &line);
  void _broadcast(const String &json);
  String _historyJson(int sinceSeq) const;

  AsyncWebServer   *_server = nullptr;
  AsyncWebSocket   *_ws     = nullptr;

  String _user, _pass;
  String _title      = "JouleSerial";
  String _brandColor = "#2ee5a0";
  bool   _mirrorHw   = true;

  struct Line { uint32_t seq; uint32_t ms; uint8_t lvl; String text; };
  std::deque<Line> _history;
  size_t   _historyMax = 256;
  uint32_t _seq        = 0;
  String   _writeBuf;             // Print interface accumulator

  SerialMessageCb _onMessage;
};

} // namespace joule

extern joule::JouleSerialClass JouleSerial;
