// 02 Wi-Fi: setup() starts the selected mode; loop() checks connectivity or the AP client count.
//
// Set the mode, SSID, password, and connection parameters in app_config.h.
// #if compiles only the selected branch; AP provides a LAN hotspot, not automatic Internet forwarding.

#include <Arduino.h>
#include <WiFi.h>
#include "app_config.h"

#if APP_WIFI_MODE == APP_WIFI_MODE_STA

namespace {
uint32_t lastConnectAttemptMs = 0;

bool credentialsConfigured() {
  return APP_WIFI_SSID[0] != '\0';
}

void printStaInfo() {
  Serial.println(F("connected"));
  Serial.printf("IP   : %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("RSSI : %d dBm\n", WiFi.RSSI());
  Serial.printf("MAC  : %s\n", WiFi.macAddress().c_str());
}

bool connectSta() {
  // The retry interval starts with this attempt and includes the connection wait configured below.
  lastConnectAttemptMs = millis();
  if (!credentialsConfigured()) {
    Serial.println(F("Set APP_WIFI_SSID and APP_WIFI_PASSWORD in app_config.h."));
    return false;
  }

  Serial.printf("connecting to: %s\n", APP_WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(APP_WIFI_SSID, APP_WIFI_PASSWORD);

  // This chapter waits synchronously with a timeout; loop() does not run during the wait.
  // For concurrent UI/sensor handling later, switch to events rather than adding more polling here.
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < APP_WIFI_CONNECT_TIMEOUT_MS) {
    Serial.print('.');
    delay(500);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    Serial.println(F("connection timeout; will retry later"));
    return false;
  }

  printStaInfo();
  return true;
}
}  // namespace

void setupWifiDemo() {
  Serial.println(F("mode: STA (join an existing router)"));
  connectSta();
}

void loopWifiDemo() {
  if (WiFi.status() == WL_CONNECTED) {
    static uint32_t lastPrintMs = 0;
    if (millis() - lastPrintMs >= 5000) {
      lastPrintMs = millis();
      Serial.printf("online, RSSI=%d dBm\n", WiFi.RSSI());
    }
    return;
  }

  if (credentialsConfigured() &&
      millis() - lastConnectAttemptMs >= APP_WIFI_RETRY_INTERVAL_MS) {
    connectSta();
  }
}

#else  // APP_WIFI_MODE_AP

namespace {
int lastStationCount = -1;
uint32_t lastCheckMs = 0;
}  // namespace

void setupWifiDemo() {
  Serial.println(F("mode: AP (create a local hotspot)"));
  WiFi.mode(WIFI_AP);

  if (!WiFi.softAP(APP_WIFI_AP_SSID, APP_WIFI_AP_PASSWORD,
                   APP_WIFI_AP_CHANNEL, 0, APP_WIFI_AP_MAX_CONNECTIONS)) {
    Serial.println(F("AP start failed"));
    return;
  }

  Serial.printf("SSID : %s\n", APP_WIFI_AP_SSID);
  Serial.printf("IP   : %s\n", WiFi.softAPIP().toString().c_str());
  Serial.println(F("The AP does not provide Internet access."));
}

void loopWifiDemo() {
  if (millis() - lastCheckMs < 1000) return;
  lastCheckMs = millis();

  const int stationCount = WiFi.softAPgetStationNum();
  if (stationCount != lastStationCount) {
    lastStationCount = stationCount;
    Serial.printf("connected devices: %d\n", stationCount);
  }
}

#endif

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("[02] Wi-Fi modes"));
  setupWifiDemo();
}

void loop() {
  loopWifiDemo();
  // Avoid a busy loop without a wait; this delay does not drive the underlying Wi-Fi stack.
  delay(20);
}

