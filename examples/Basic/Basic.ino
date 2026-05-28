// ---------------------------------------------------------------------------
// JouleSuite for ESP32 / ESP8266 — JouleOTA · JouleSerial · JouleNet · JouleDash
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

void setup(){
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin("YOUR_SSID","YOUR_PASS");
  while (WiFi.status() != WL_CONNECTED) delay(200);
  JouleSerial.begin(&server, "admin","joule");
  JouleSerial.onMessage([](const String &cmd){
    JouleSerial.inf("you typed: %s", cmd.c_str());
    if (cmd == "reboot") ESP.restart();
  });
  server.begin();
  JouleSerial.inf("hello from %s at %s", WiFi.macAddress().c_str(), WiFi.localIP().toString().c_str());
}

void loop(){
  if (millis() - lastTick > 2000) {
    lastTick = millis();
    JouleSerial.dbg("heap=%u rssi=%d", ESP.getFreeHeap(), WiFi.RSSI());
  }
}
