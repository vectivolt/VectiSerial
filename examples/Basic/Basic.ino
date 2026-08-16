// ---------------------------------------------------------------------------
// VectiSuite for ESP32 — VectiOTA · VectiSerial · VectiNet · VectiDash
// Author: VectiVolt team
// (c) 2026 VectiVolt — Apache-2.0 License
// ---------------------------------------------------------------------------

// Minimal VectiSerial example. Open http://<ip>/serial in a browser.
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <VectiSerial.h>

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
  VectiSerial.begin(&server, "admin","vecti");
  VectiSerial.onMessage([](const String &cmd){
    VectiSerial.inf("you typed: %s", cmd.c_str());
    if (cmd == "reboot") rebootRequested = true;
  });
  server.begin();
  VectiSerial.inf("hello from %s at %s", WiFi.macAddress().c_str(), WiFi.localIP().toString().c_str());
}

void loop(){
  VectiSerial.loop();
  if (rebootRequested) ESP.restart();
  if (millis() - lastTick > 2000) {
    lastTick = millis();
    VectiSerial.dbg("heap=%u rssi=%d", ESP.getFreeHeap(), WiFi.RSSI());
  }
}
