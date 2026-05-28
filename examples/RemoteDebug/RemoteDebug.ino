// ---------------------------------------------------------------------------
// JouleSuite for ESP32 / ESP8266 — JouleOTA · JouleSerial · JouleNet · JouleDash
// Author: Chinmoy Bhuyan
// Email:  dikibhuyan@gmail.com
// (c) 2026 — MIT License
// ---------------------------------------------------------------------------
//
// RemoteDebug — production logger with module tags. Use the supplied
// LOG macros from any .cpp/.h to get auto-prefixed, level-tinted log
// lines that show up live in JouleSerial's web console *and* on the
// hardware UART. No #ifdef debug rituals, no string-concatenation
// overhead at runtime.

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <JouleSerial.h>

#define LOGD(mod, fmt, ...)  JouleSerial.dbg("[%s] " fmt, mod, ##__VA_ARGS__)
#define LOGI(mod, fmt, ...)  JouleSerial.inf("[%s] " fmt, mod, ##__VA_ARGS__)
#define LOGW(mod, fmt, ...)  JouleSerial.wrn("[%s] " fmt, mod, ##__VA_ARGS__)
#define LOGE(mod, fmt, ...)  JouleSerial.err("[%s] " fmt, mod, ##__VA_ARGS__)

AsyncWebServer server(80);

void wifiTask() {
  static uint32_t last = 0;
  if (millis() - last < 5000) return;
  last = millis();
  int rssi = WiFi.RSSI();
  if      (rssi < -85) LOGE("wifi", "rssi=%d very weak", rssi);
  else if (rssi < -75) LOGW("wifi", "rssi=%d weak",      rssi);
  else                 LOGI("wifi", "rssi=%d ok",         rssi);
}

void sensorTask() {
  static uint32_t last = 0;
  if (millis() - last < 2000) return;
  last = millis();
  float t = 22.0f + 5.0f * sin(millis() / 10000.0f);
  LOGD("sensor", "temp=%.2f °C  raw=%d", t, analogRead(34));
  if (t > 30) LOGW("sensor", "over-temperature");
}

void setup() {
  Serial.begin(115200);
  WiFi.begin("YOUR_SSID","YOUR_PASS");
  while (WiFi.status() != WL_CONNECTED) delay(200);

  JouleSerial.setHistorySize(1024);
  JouleSerial.begin(&server);
  server.begin();

  LOGI("boot", "JouleSuite RemoteDebug example online");
  LOGI("boot", "ip=%s mac=%s", WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str());
}

void loop() {
  wifiTask();
  sensorTask();
  delay(10);
}
