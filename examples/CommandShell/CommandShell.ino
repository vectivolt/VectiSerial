// ---------------------------------------------------------------------------
// JouleSuite for ESP32 / ESP8266 — JouleOTA · JouleSerial · JouleNet · JouleDash
// Author: Chinmoy Bhuyan
// Email:  dikibhuyan@gmail.com
// (c) 2026 — MIT License
// ---------------------------------------------------------------------------
//
// CommandShell — a tiny REPL on top of JouleSerial. Register commands at
// boot, type them in the wireless console, and the handlers fire just
// like an SSH shell. Great for ops / field diagnostics.

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <JouleSerial.h>
#include <map>
#include <functional>

AsyncWebServer server(80);

// ---- micro REPL ------------------------------------------------------------
using CmdFn = std::function<void(const String &args)>;
std::map<String, CmdFn> commands;
void cmd(const char *name, CmdFn fn) { commands[String(name)] = std::move(fn); }

void dispatch(const String &line) {
  int sp = line.indexOf(' ');
  String head = sp < 0 ? line : line.substring(0, sp);
  String args = sp < 0 ? ""   : line.substring(sp + 1);
  auto it = commands.find(head);
  if (it == commands.end()) {
    JouleSerial.wrn("unknown command '%s' — try 'help'", head.c_str());
    return;
  }
  it->second(args);
}

void setup() {
  Serial.begin(115200);
  WiFi.begin("YOUR_SSID","YOUR_PASS");
  while (WiFi.status() != WL_CONNECTED) delay(200);

  JouleSerial.setTitle("Production console");
  JouleSerial.begin(&server, "admin", "joule");
  server.begin();

  // ---- built-in commands -------------------------------------------------
  cmd("help", [](const String &) {
    JouleSerial.inf("available commands:");
    for (auto &kv : commands) JouleSerial.inf("  %s", kv.first.c_str());
  });
  cmd("heap", [](const String &) { JouleSerial.inf("heap = %u bytes", ESP.getFreeHeap()); });
  cmd("rssi", [](const String &) { JouleSerial.inf("rssi = %d dBm", WiFi.RSSI()); });
  cmd("ip",   [](const String &) { JouleSerial.inf("ip = %s", WiFi.localIP().toString().c_str()); });
  cmd("uptime", [](const String &) { JouleSerial.inf("uptime = %lu s", millis()/1000); });
  cmd("echo", [](const String &args) { JouleSerial.inf("%s", args.c_str()); });
  cmd("reboot", [](const String &) { JouleSerial.wrn("reboot in 1s…"); delay(1000); ESP.restart(); });
  cmd("set", [](const String &args) {
    // Example: "set led 1" toggles GPIO 2
    int sp = args.indexOf(' '); if (sp < 0) { JouleSerial.err("usage: set <pin> <0|1>"); return; }
    int pin = args.substring(0, sp).toInt();
    int val = args.substring(sp + 1).toInt();
    pinMode(pin, OUTPUT); digitalWrite(pin, val);
    JouleSerial.inf("set pin %d = %d", pin, val);
  });

  JouleSerial.onMessage(dispatch);
  JouleSerial.inf("ready — type 'help' for a list of commands");
}

void loop() { delay(10); }
