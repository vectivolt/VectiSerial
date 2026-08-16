// ---------------------------------------------------------------------------
// JouleSuite for ESP32 — JouleOTA · JouleSerial · JouleNet · JouleDash
// Author: Chinmoy Bhuyan
// Email:  dikibhuyan@gmail.com
// (c) 2026 — MIT License
// ---------------------------------------------------------------------------

// Minimal JouleSerial example. Open http://<ip>/serial in a browser.
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <JouleSerial.h>

AsyncWebServer server(80);
unsigned long lastTick = 0;

// onMessage() runs on the AsyncTCP task — rebooting from there tears the TCP
// stack down from inside its own callback, so loop() does it instead.
volatile bool rebootRequested = false;

void setup(){
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin("YOUR_SSID","YOUR_PASS");
  while (WiFi.status() != WL_CONNECTED) delay(200);
  JouleSerial.begin(&server, "admin","joule");
  JouleSerial.onMessage([](const String &cmd){
    JouleSerial.inf("you typed: %s", cmd.c_str());
    if (cmd == "reboot") rebootRequested = true;
  });
  server.begin();
  JouleSerial.inf("hello from %s at %s", WiFi.macAddress().c_str(), WiFi.localIP().toString().c_str());
}

void loop(){
  JouleSerial.loop();
  if (rebootRequested) ESP.restart();
  if (millis() - lastTick > 2000) {
    lastTick = millis();
    JouleSerial.dbg("heap=%u rssi=%d", ESP.getFreeHeap(), WiFi.RSSI());
  }
}
