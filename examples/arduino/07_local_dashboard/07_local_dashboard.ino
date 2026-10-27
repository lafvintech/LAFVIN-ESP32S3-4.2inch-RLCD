// ============================================================
// 07_local_dashboard -- local information dashboard (integration example)
//
// Combine the RTC from 05, SHTC3 from 06, battery ADC and buttons; observe drawing/flush timings.
// Reading order: setup() prepares the first frame -> loop() updates inputs/cache -> draw the current page as needed.
// All operations run sequentially in one loop(); different sampling intervals do not imply parallel tasks.
//
// Key concepts:
//   * Compare lookup-table and direct-calculation setPixel timings (automatic startup benchmark).
//   * Measure drawing and transfer submission (flush) separately.
//   * Use different intervals: poll time/battery every second, temperature/humidity every 10 seconds.
//     Environmental data changes slowly; frequent measurements waste I2C bandwidth and power.
//
// Controls: KEY switches pages. Page 1: large clock/environment overview. Page 2: details/performance.
// Enter "YYYY-MM-DD HH:MM:SS" in Serial to set the RTC at any time.
// ============================================================

#include <Arduino.h>
#include "board_pins.h"
#include "display_bsp.h"
#include "draw_bsp.h"
#include "i2c_bus_bsp.h"
#include "rtc_bsp.h"
#include "shtc3_bsp.h"
#include "battery_bsp.h"
#include "buttons_bsp.h"

namespace {
RlcdPanel panel(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC,
                PIN_LCD_CS, PIN_LCD_RST,
                LCD_WIDTH_LANDSCAPE, LCD_HEIGHT_LANDSCAPE);
I2cBus i2c;
Pcf85063 rtc;
Shtc3Sensor shtc3;
DebouncedButton keyButton(PIN_KEY_USER);
DebouncedButton bootButton(PIN_KEY_BOOT);

// ---- Dashboard cache: sampling functions write; pages only read and never access sensors while drawing ----
RtcDateTime g_now;
bool g_rtcReady = false;
bool g_rtcOk = false;      // Only indicates a successful read; also check the flags below before trusting the time.
bool g_rtcOsFlag = false;  // This example combines OS-set and out-of-range date errors.
Shtc3Sample g_env = {0, 0};
bool g_envOk = false;
float g_battV = 0;
uint8_t g_battPct = 0;
bool g_page2 = false;

// ---- Performance statistics (Serial and page 2) ----
uint32_t g_lastDrawMs = 0;
uint32_t g_lastFlushMs = 0;

// ---- Update intervals ----
uint32_t g_lastFastUpdate = 0;    // Time/battery: 1 s.
uint32_t g_lastEnvUpdate = 0;     // Temperature/humidity: 10 s.

const char *kWeekdayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

bool isLeapYear(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && isLeapYear(year) ? 29 : kDays[month - 1];
}

bool isValidDateTime(const RtcDateTime &t) {
  return t.year >= 2000 && t.year <= 2099 &&
         t.month >= 1 && t.month <= 12 &&
         t.day >= 1 && t.day <= daysInMonth(t.year, t.month) &&
         t.weekday <= 6 && t.hour <= 23 && t.minute <= 59 && t.second <= 59;
}

uint8_t calculateWeekday(uint16_t year, uint8_t month, uint8_t day) {
  // Sakamoto algorithm, 0=Sunday; January/February are treated as part of the previous year.
  static const uint8_t kMonthOffsets[] = {0, 3, 2, 5, 0, 3,
                                          5, 1, 4, 6, 2, 4};
  uint16_t y = year;
  if (month < 3) --y;
  return (uint8_t)((y + y / 4 - y / 100 + y / 400 +
                    kMonthOffsets[month - 1] + day) % 7);
}

bool parseTimeLine(const String &line, RtcDateTime *out) {
  int year, month, day, hour, minute, second;
  if (sscanf(line.c_str(), "%d-%d-%d %d:%d:%d",
             &year, &month, &day, &hour, &minute, &second) != 6) return false;
  if (year < 2000 || year > 2099 || month < 1 || month > 12 ||
      day < 1 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) return false;

  RtcDateTime t;
  t.year = (uint16_t)year;
  t.month = (uint8_t)month;
  t.day = (uint8_t)day;
  t.hour = (uint8_t)hour;
  t.minute = (uint8_t)minute;
  t.second = (uint8_t)second;
  t.weekday = calculateWeekday(t.year, t.month, t.day);
  if (!isValidDateTime(t)) return false;

  *out = t;
  return true;
}

bool readSerialLine(String *lineOut) {
  static String pending;
  while (Serial.available()) {
    const char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      *lineOut = pending;
      pending = "";
      return true;
    }
    pending += ch;
    if (pending.length() > 40) pending = "";
  }
  return false;
}

// Collect complete Serial lines only; read back after writing and redraw on success or validation failure.
void handleTimeInput(bool &needRedraw) {
  String line;
  if (readSerialLine(&line)) {
    RtcDateTime wanted;
    if (!parseTimeLine(line, &wanted)) {
      Serial.println(F("Bad format. Example: 2026-08-28 12:34:56"));
    } else if (!g_rtcReady || !rtc.write(wanted)) {
      Serial.println(F("RTC write FAILED"));
    } else {
      RtcDateTime verified;
      bool osFlag = true;
      if (rtc.read(&verified, &osFlag) && !osFlag && isValidDateTime(verified)) {
        g_now = verified;
        g_rtcOk = true;
        g_rtcOsFlag = false;
        needRedraw = true;
        Serial.printf("RTC set OK -> %04u-%02u-%02u %02u:%02u:%02u\n",
                      verified.year, verified.month, verified.day,
                      verified.hour, verified.minute, verified.second);
      } else {
        g_rtcOk = false;
        g_rtcOsFlag = false;
        needRedraw = true;
        Serial.println(F("RTC write completed, but read-back validation FAILED"));
      }
    }
  }
}

void runStartupBenchmark() {
  // Visit the same pixels in RAM without SPI transfers; compare measurements rather than assuming lookup is faster.
  const int16_t w = panel.width();
  const int16_t h = panel.height();

  uint32_t t0 = millis();
  for (int16_t y = 0; y < h; y++)
    for (int16_t x = 0; x < w; x++)
      panel.setPixelSlow(x, y, ((x ^ y) & 1) ? LCD_PIXEL_BLACK : LCD_PIXEL_WHITE);
  uint32_t directMs = millis() - t0;

  t0 = millis();
  for (int16_t y = 0; y < h; y++)
    for (int16_t x = 0; x < w; x++)
      panel.setPixel(x, y, ((x ^ y) & 1) ? LCD_PIXEL_BLACK : LCD_PIXEL_WHITE);
  uint32_t lutMs = millis() - t0;

  Serial.printf("benchmark full-frame: direct=%lu ms, LUT=%lu ms\n",
                (unsigned long)directMs, (unsigned long)lutMs);
}

void readFastInputs() {
  RtcDateTime t;
  bool os = false;
  if (g_rtcReady && rtc.read(&t, &os)) {
    g_now = t;
    g_rtcOk = true;
    g_rtcOsFlag = os || !isValidDateTime(t);
  } else {
    g_rtcOk = false;
    g_rtcOsFlag = false;
  }

  float v = batteryReadVoltage();
  // Update the cache only for positive voltages; retain the previous value if sampling fails or returns 0.
  if (v > 0) {
    g_battV = v;
    g_battPct = batteryEstimatePercent(v);
  }
}

void readSlowInputs() {
  Shtc3Sample s;
  if (shtc3.measure(&s) == SHTC3_OK) {
    g_env = s;
    g_envOk = true;
  } else {
    g_envOk = false;
  }
}

// Draw one centered line of scaled text.
void drawCenteredText(const char *text, int16_t y, uint8_t scale,
                      uint8_t color) {
  int16_t w = textWidth(text, scale);
  drawString(panel, (panel.width() - w) / 2, y, text, color, scale);
}

void drawInfoBox(int16_t x, int16_t y, int16_t w, int16_t h,
                 const char *caption, const char *value) {
  drawRect(panel, x, y, w, h, LCD_PIXEL_BLACK);
  drawString(panel, x + 8, y + 8, caption, LCD_PIXEL_BLACK, 1);
  drawString(panel, x + 8, y + 22, value, LCD_PIXEL_BLACK, 2);
}

char timeBuf[12];
char dateBuf[24];
char tempBuf[12];
char humBuf[12];
char battBuf[14];

void drawPage1() {
  panel.clearBuffer(LCD_PIXEL_WHITE);

  // Large HH:MM:SS clock (scale 4 => 32 px tall).
  snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u",
           g_now.hour, g_now.minute, g_now.second);
  drawCenteredText(timeBuf, 46, 4, LCD_PIXEL_BLACK);

  snprintf(dateBuf, sizeof(dateBuf), "%04u-%02u-%02u  %s",
           g_now.year, g_now.month, g_now.day,
           kWeekdayNames[g_now.weekday % 7]);
  drawCenteredText(dateBuf, 86, 1, LCD_PIXEL_BLACK);

  if (g_envOk) {
    snprintf(tempBuf, sizeof(tempBuf), "%.1fC", g_env.temperatureC);
    snprintf(humBuf, sizeof(humBuf), "%.0f%%", g_env.humidityPct);
  } else {
    strcpy(tempBuf, "--");
    strcpy(humBuf, "--");
  }
  drawInfoBox(30, 120, 160, 60, "TEMP", tempBuf);
  drawInfoBox(210, 120, 160, 60, "HUMI", humBuf);

  if (!g_rtcOk) {
    drawString(panel, 8, 8, "RTC ERR", LCD_PIXEL_BLACK, 1);
  }
  if (g_rtcOsFlag) {
    drawString(panel, 70, 8, "RTC TIME UNRELIABLE", LCD_PIXEL_BLACK, 1);
  }

  snprintf(battBuf, sizeof(battBuf), "BAT %u%% %.2fV",
           g_battPct, (double)g_battV);
  drawString(panel, 8, panel.height() - 20, battBuf, LCD_PIXEL_BLACK, 1);
  drawRect(panel, panel.width() - 108, panel.height() - 22, 100, 14,
           LCD_PIXEL_BLACK);
  fillRect(panel, panel.width() - 106, panel.height() - 20,
           (int16_t)(g_battPct), 10, LCD_PIXEL_BLACK);   // Battery bar.
}

void drawPage2() {
  // The new frame is not complete yet; last draw/flush on the page describes the previous refresh.
  panel.clearBuffer(LCD_PIXEL_WHITE);

  drawString(panel, 16, 20, "DETAIL / STATS", LCD_PIXEL_BLACK, 2);

  char line[48];
  snprintf(line, sizeof(line), "battery   : %.3f V (%u%%)",
           (double)g_battV, g_battPct);
  drawString(panel, 16, 64, line, LCD_PIXEL_BLACK, 1);

  if (g_envOk) {
    snprintf(line, sizeof(line), "temp      : %.2f C", (double)g_env.temperatureC);
    drawString(panel, 16, 82, line, LCD_PIXEL_BLACK, 1);
    snprintf(line, sizeof(line), "humidity  : %.2f %%RH", (double)g_env.humidityPct);
    drawString(panel, 16, 100, line, LCD_PIXEL_BLACK, 1);
  } else {
    drawString(panel, 16, 82, "env       : sensor error", LCD_PIXEL_BLACK, 1);
  }

  snprintf(line, sizeof(line), "uptime    : %lu s", millis() / 1000);
  drawString(panel, 16, 126, line, LCD_PIXEL_BLACK, 1);

  snprintf(line, sizeof(line), "last draw : %lu ms", (unsigned long)g_lastDrawMs);
  drawString(panel, 16, 152, line, LCD_PIXEL_BLACK, 1);
  snprintf(line, sizeof(line), "last flush: %lu ms", (unsigned long)g_lastFlushMs);
  drawString(panel, 16, 170, line, LCD_PIXEL_BLACK, 1);

  drawString(panel, 16, 204, "press KEY to page 1", LCD_PIXEL_BLACK, 1);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("[07] local dashboard"));

  if (!panel.begin()) {
    Serial.println(F("PANEL_INIT: FAIL"));
    while (true) delay(1000);
  }
  if (!i2c.begin(PIN_I2C_SDA, PIN_I2C_SCL)) {
    Serial.println(F("I2C init FAIL"));
  }
  g_rtcReady = rtc.begin(i2c);
  if (!g_rtcReady) Serial.println(F("RTC init FAIL"));
  Serial.println(F("Send YYYY-MM-DD HH:MM:SS to correct RTC time."));
  shtc3.begin(i2c);
  batteryInit();

  runStartupBenchmark();

  readFastInputs();   // Populate data before drawing the first frame.
  readSlowInputs();
  drawPage1();
  uint32_t t0 = millis();
  panel.flush();
  g_lastFlushMs = millis() - t0;
  Serial.printf("first flush: %lu ms\n", (unsigned long)g_lastFlushMs);
}

void loop() {
  keyButton.update();
  bootButton.update();

  bool needRedraw = false;
  uint32_t now = millis();

  handleTimeInput(needRedraw);

  // KEY switches pages; BOOT only redraws the cache without triggering another sensor sample.
  if (keyButton.consumePress()) {
    g_page2 = !g_page2;
    needRedraw = true;
  }
  if (bootButton.consumePress()) {
    needRedraw = true;
  }

  // Fast updates: time/battery every second.
  if (now - g_lastFastUpdate >= 1000) {
    g_lastFastUpdate = now;
    readFastInputs();
    if (!g_page2) needRedraw = true;
  }

  // Slow updates: temperature/humidity every 10 seconds. Slowly changing data needs fewer sensor accesses;
  // each avoided I2C measurement saves power and bus bandwidth.
  if (now - g_lastEnvUpdate >= 10000) {
    g_lastEnvUpdate = now;
    readSlowInputs();
    needRedraw = true;
  }

  if (needRedraw) {
    uint32_t t0 = millis();
    if (g_page2) drawPage2();
    else         drawPage1();
    g_lastDrawMs = millis() - t0;

    t0 = millis();
    // The Arduino driver only submits an asynchronous transfer; this is not the full-frame SPI transfer time.
    panel.flush();
    g_lastFlushMs = millis() - t0;

    Serial.printf("refresh: draw=%lu ms flush=%lu ms\n",
                  (unsigned long)g_lastDrawMs, (unsigned long)g_lastFlushMs);
  }

  delay(10);
}
