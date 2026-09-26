#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

#ifndef WLED_REMOTE_VERSION
#define WLED_REMOTE_VERSION "dev"
#endif

struct WledDevice {
  String name;
  IPAddress ip;
  bool online = false;
  bool on = false;
  uint8_t brightness = 0;
};

static constexpr size_t MAX_DEVICES = 16;
WledDevice devices[MAX_DEVICES];
size_t deviceCount = 0;
size_t selected = 0;
unsigned long lastRefresh = 0;

void drawHeader(const char* subtitle) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.print("WLED REMOTE");
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(9, 30);
  M5.Display.print(subtitle);
}

void drawStatus(const String& message) {
  drawHeader(WLED_REMOTE_VERSION);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 70);
  M5.Display.println(message);
}

bool queryDevice(WledDevice& d) {
  HTTPClient http;
  http.setTimeout(1200);
  String url = "http://" + d.ip.toString() + "/json";
  if (!http.begin(url)) return false;
  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return false;

  d.name = doc["info"]["name"] | d.name;
  d.on = doc["state"]["on"] | false;
  d.brightness = doc["state"]["bri"] | 0;
  d.online = true;
  return true;
}

bool sendState(WledDevice& d, const String& json) {
  HTTPClient http;
  http.setTimeout(1500);
  if (!http.begin("http://" + d.ip.toString() + "/json/state")) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  http.end();
  if (code < 200 || code >= 300) return false;
  delay(80);
  return queryDevice(d);
}

void toggleDevice(WledDevice& d) {
  sendState(d, d.on ? "{\"on\":false}" : "{\"on\":true}");
}

void setWhite(WledDevice& d) {
  sendState(d, "{\"on\":true,\"seg\":[{\"col\":[[255,255,255]]}]}");
}

void restoreDefault(WledDevice& d) {
  sendState(d, "{\"on\":true,\"ps\":1}");
}

void setSleepTimer(WledDevice& d, uint16_t minutes) {
  String payload = "{\"on\":true,\"nl\":{\"on\":true,\"dur\":" + String(minutes) +
                   ",\"mode\":0,\"tbri\":0}}";
  sendState(d, payload);
}

void discoverWled() {
  deviceCount = 0;
  drawStatus("DISCOVERING...");
  int n = MDNS.queryService("wled", "tcp");
  for (int i = 0; i < n && deviceCount < MAX_DEVICES; ++i) {
    IPAddress ip = MDNS.IP(i);
    bool duplicate = false;
    for (size_t j = 0; j < deviceCount; ++j) {
      if (devices[j].ip == ip) duplicate = true;
    }
    if (duplicate) continue;

    WledDevice d;
    d.ip = ip;
    d.name = MDNS.hostname(i);
    if (queryDevice(d)) devices[deviceCount++] = d;
  }
}

void drawDevices() {
  drawHeader(WiFi.localIP().toString().c_str());
  M5.Display.setTextSize(1);
  if (!deviceCount) {
    M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    M5.Display.setCursor(10, 65);
    M5.Display.println("NO WLED LIGHTS FOUND");
    M5.Display.setCursor(10, 82);
    M5.Display.println("B: RESCAN");
    return;
  }

  for (size_t i = 0; i < deviceCount && i < 6; ++i) {
    int y = 50 + (int)i * 20;
    M5.Display.setTextColor(i == selected ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(8, y);
    M5.Display.print(i == selected ? "> " : "  ");
    String name = devices[i].name;
    if (name.length() > 14) name = name.substring(0, 14);
    M5.Display.print(name);
    M5.Display.setCursor(190, y);
    M5.Display.setTextColor(devices[i].on ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
    M5.Display.print(devices[i].on ? "ON" : "OFF");
  }
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setCursor(8, 125);
  M5.Display.printf("A TOGGLE   B NEXT   %u FOUND", (unsigned)deviceCount);
}

void connectSavedWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  drawStatus("CONNECTING...");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
    delay(100);
    M5.update();
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(100);

  connectSavedWifi();
  if (WiFi.status() != WL_CONNECTED) {
    drawStatus("WIFI SETUP NEXT");
    // Captive-portal provisioning is the next implementation step.
    return;
  }

  if (!MDNS.begin("wled-remote")) {
    drawStatus("MDNS ERROR");
    return;
  }

  discoverWled();
  drawDevices();
}

void loop() {
  M5.update();
  if (WiFi.status() != WL_CONNECTED) {
    delay(20);
    return;
  }

  if (M5.BtnA.wasPressed() && deviceCount) {
    toggleDevice(devices[selected]);
    drawDevices();
  }

  if (M5.BtnB.wasPressed()) {
    if (deviceCount) selected = (selected + 1) % deviceCount;
    else discoverWled();
    drawDevices();
  }

  if (millis() - lastRefresh > 10000) {
    lastRefresh = millis();
    for (size_t i = 0; i < deviceCount; ++i) queryDevice(devices[i]);
    drawDevices();
  }
  delay(15);
}
