// ============================================================
// 08_network_weather -- simple Wi-Fi weather station
//
// Flow: initialize hardware -> connect Wi-Fi -> synchronize via NTP -> fetch weather -> display.
// Reading order: this file schedules samples/retries; board.cpp handles hardware; weather.cpp fetches/draws weather.
// Network connection, time sync and HTTP wait synchronously here; KEY scanning/page updates pause meanwhile.
// KEY: try to connect when offline; refresh weather when online without forcing a disconnect.
// Set Wi-Fi, city, time zone and refresh intervals in app_config.h; fixed pins are in board.h.
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "app_config.h"

#include "board.h"
#include "weather.h"

namespace {
RlcdPanel panel(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC,
                PIN_LCD_CS, PIN_LCD_RST, LCD_WIDTH, LCD_HEIGHT);
DebouncedButton keyButton(PIN_KEY);
DashboardData dashboard;

bool rtcPresent = false;
bool shtc3Present = false;
bool ntpSynced = false;
// dirty coalesces multiple changes; drawIfNeeded() redraws the full page only when required.
bool dirty = true;
uint8_t drawnMinute = 0xFF;

uint32_t lastRtcReadMs = 0;
uint32_t lastIndoorReadMs = 0;
uint32_t lastWeatherAttemptMs = 0;
uint32_t lastWifiAttemptMs = 0;

bool wifiConfigured() {
  return APP_WIFI_SSID[0] != '\0';
}

bool validRtc(const RtcDateTime &value) {
  return value.year >= 2000 && value.year <= 2099 &&
         value.month >= 1 && value.month <= 12 &&
         value.day >= 1 && value.day <= 31 &&
         value.weekday <= 6 && value.hour <= 23 &&
         value.minute <= 59 && value.second <= 59;
}

void readRtc() {
  const bool wasReady = dashboard.rtcReady;
  lastRtcReadMs = millis();
  if (!rtcPresent) return;

  RtcDateTime value;
  bool clockInvalid = false;
  if (rtcRead(&value, &clockInvalid) && !clockInvalid && validRtc(value)) {
    dashboard.rtc = value;
    dashboard.rtcReady = true;
  } else {
    dashboard.rtcReady = false;
  }
  if (wasReady != dashboard.rtcReady) dirty = true;
}

void readIndoor() {
  lastIndoorReadMs = millis();
  if (!shtc3Present) return;

  Shtc3Sample sample;
  // On failure, retain the last successful indoor data; indoorReady does not mean the latest measurement succeeded.
  if (shtc3Measure(&sample)) {
    dashboard.indoor = sample;
    dashboard.indoorReady = true;
    dirty = true;
  }
}

void updateWifiState() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != dashboard.wifiConnected) {
    dashboard.wifiConnected = connected;
    dirty = true;
  }
}

bool connectWifi() {
  lastWifiAttemptMs = millis();
  if (WiFi.status() == WL_CONNECTED) {
    updateWifiState();
    return true;
  }

  if (!wifiConfigured()) {
    snprintf(dashboard.message, sizeof(dashboard.message),
             "set Wi-Fi in app_config.h");
    dirty = true;
    return false;
  }

  drawStatusPage(panel, "connecting...", APP_WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(APP_WIFI_SSID, APP_WIFI_PASSWORD);

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < APP_WIFI_TIMEOUT_MS) {
    delay(100);
  }

  updateWifiState();
  if (!dashboard.wifiConnected) {
    WiFi.disconnect();
    snprintf(dashboard.message, sizeof(dashboard.message),
             "WiFi timeout; press KEY");
    Serial.println(F("[wifi] connection timeout"));
    dirty = true;
    return false;
  }

  snprintf(dashboard.message, sizeof(dashboard.message),
           "WiFi connected: %s", WiFi.localIP().toString().c_str());
  Serial.printf("[wifi] connected: %s\n",
                WiFi.localIP().toString().c_str());
  dirty = true;
  return true;
}

void syncClock() {
  // After starting SNTP, obtain system local time, then try to write the separate RTC.
  // The page uses readRtc(), not the system clock directly.
  drawStatusPage(panel, "syncing time...", APP_NTP_SERVER);
  configTzTime(APP_TIME_ZONE, APP_NTP_SERVER);

  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 10000)) {
    snprintf(dashboard.message, sizeof(dashboard.message),
             dashboard.rtcReady ? "NTP timeout; using RTC"
                                : "NTP timeout; no clock");
    Serial.println(F("[ntp] timeout"));
    dirty = true;
    return;
  }

  ntpSynced = true;
  // This only records that getLocalTime() obtained usable system time, not that a new NTP response arrived
  // or that writing the RTC succeeded; it is not a strict "latest network time sync succeeded" flag.
  if (rtcPresent) {
    RtcDateTime value;
    value.year = timeInfo.tm_year + 1900;
    value.month = timeInfo.tm_mon + 1;
    value.day = timeInfo.tm_mday;
    value.weekday = timeInfo.tm_wday;
    value.hour = timeInfo.tm_hour;
    value.minute = timeInfo.tm_min;
    value.second = timeInfo.tm_sec;
    rtcWrite(value);
    readRtc();
  }
  Serial.println(F("[ntp] time synchronized"));
}

void updateWeather() {
  // Update the attempt time even on failure to avoid continuous requests in every loop while offline or the service is down.
  lastWeatherAttemptMs = millis();
  drawStatusPage(panel, "loading weather...", APP_WEATHER_CITY);

  WeatherData latest;
  if (fetchWeather(APP_WEATHER_LATITUDE, APP_WEATHER_LONGITUDE, &latest)) {
    dashboard.weather = latest;
    dashboard.weatherReady = true;
    if (ntpSynced) {
      snprintf(dashboard.message, sizeof(dashboard.message),
               "weather updated %s", latest.fetchedAt);
    } else {
      snprintf(dashboard.message, sizeof(dashboard.message),
               "weather updated; NTP timeout");
    }
    Serial.printf("[weather] %.1f C\n", (double)latest.temperatureC);
  } else {
    // On failure, update only the message; preserve the last successful data.
    snprintf(dashboard.message, sizeof(dashboard.message),
             "weather error: %.55s", latest.error);
    Serial.printf("[weather] %s\n", latest.error);
  }
  dirty = true;
}

void connectAndUpdate() {
  if (!connectWifi()) return;
  if (!ntpSynced) syncClock();
  updateWeather();
}

void drawIfNeeded() {
  // Read RTC every second, but display only minutes; indoor values/network changes can still trigger a full redraw.
  const bool minuteChanged = dashboard.rtcReady &&
                             dashboard.rtc.minute != drawnMinute;
  if (!dirty && !minuteChanged) return;

  drawWeatherPage(panel, dashboard, APP_WEATHER_CITY);
  drawnMinute = dashboard.rtcReady ? dashboard.rtc.minute : 0xFF;
  dirty = false;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("[08] simple network weather"));
  keyButton.begin();

  if (!panel.begin()) {
    Serial.println(F("[panel] init failed"));
    while (true) delay(1000);
  }
  drawStatusPage(panel, "starting...", "08 weather station");

  const bool i2cReady = boardI2cBegin();
  rtcPresent = i2cReady && rtcBegin();
  shtc3Present = i2cReady && shtc3Begin();
  Serial.printf("[rtc] %s\n", rtcPresent ? "found" : "missing");
  Serial.printf("[shtc3] %s\n", shtc3Present ? "found" : "missing");

  readRtc();
  readIndoor();
  connectAndUpdate();
  dirty = true;
}

void loop() {
  keyButton.update();
  updateWifiState();
  uint32_t now = millis();

  if (now - lastRtcReadMs >= APP_RTC_READ_MS) readRtc();
  if (now - lastIndoorReadMs >= APP_INDOOR_SAMPLE_MS) readIndoor();

  if (keyButton.consumePress()) {
    connectAndUpdate();
  }

  now = millis();
  // The synchronous operations above may take seconds; read the current time again before checking the next interval.
  if (wifiConfigured() && !dashboard.wifiConnected &&
      now - lastWifiAttemptMs >= APP_WIFI_RETRY_MS) {
    connectAndUpdate();
  }

  now = millis();
  if (dashboard.wifiConnected &&
      now - lastWeatherAttemptMs >= APP_WEATHER_REFRESH_MS) {
    updateWeather();
  }

  drawIfNeeded();
  delay(20);
}
