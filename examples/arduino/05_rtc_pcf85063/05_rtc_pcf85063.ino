// ============================================================
// 05_rtc_pcf85063 -- Read/write the PCF85063 real-time clock
//
// Reading path: setup() initializes -> loop() handles input/periodic reads -> rtc_bsp.cpp accesses registers.
// A standalone RTC can keep time on backup power while the MCU is off; do not write the build time on every boot.
// Trust time only if the I2C read succeeds, OS=0, and the date is valid; OS indicates oscillator stop, not battery level.
//
// Serial interaction:
//   Print valid RTC time every second; report invalid time only once rather than repeatedly printing incorrect values.
//   Enter one line in the following format and press Enter to set the RTC:
//       2026-08-26 12:34:56
// Display output:
//   Show a large clock, date, and weekday when valid; otherwise show a time-setting prompt.
// ============================================================

#include <Arduino.h>
#include "board_pins.h"
#include "display_bsp.h"
#include "draw_bsp.h"
#include "i2c_bus_bsp.h"
#include "rtc_bsp.h"

namespace {
RlcdPanel panel(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC,
                PIN_LCD_CS, PIN_LCD_RST,
                LCD_WIDTH_LANDSCAPE, LCD_HEIGHT_LANDSCAPE);
I2cBus i2c;
Pcf85063 rtc;
bool panelReady = false;
// Start periodic output only with trusted time; if it becomes invalid, wait for user correction instead of overwriting the RTC.
bool timeLooksValid = false;
uint32_t nextRtcReadMs = 0;

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
  // Sakamoto's algorithm, returning 0=Sunday; treat January/February as part of the previous year.
  static const uint8_t kMonthOffsets[] = {0, 3, 2, 5, 0, 3,
                                          5, 1, 4, 6, 2, 4};
  uint16_t y = year;
  if (month < 3) --y;
  return (uint8_t)((y + y / 4 - y / 100 + y / 400 +
                    kMonthOffsets[month - 1] + day) % 7);
}

void printTime(const RtcDateTime &t, bool osFlag) {
  Serial.printf("%04u-%02u-%02u %s %02u:%02u:%02u%s\n",
                t.year, t.month, t.day,
                kWeekdayNames[t.weekday % 7],
                t.hour, t.minute, t.second,
                osFlag ? "  [OS flag: time may be wrong]" : "");
}

void drawCenteredText(const char *text, int16_t y, uint8_t scale) {
  const int16_t x = (panel.width() - textWidth(text, scale)) / 2;
  drawString(panel, x, y, text, LCD_PIXEL_BLACK, scale);
}

void drawRtcScreen(const RtcDateTime *t, bool valid, const char *status) {
  if (!panelReady) return;

  panel.clearBuffer(LCD_PIXEL_WHITE);
  drawString(panel, 16, 16, "PCF85063 RTC", LCD_PIXEL_BLACK, 2);
  drawLine(panel, 16, 48, panel.width() - 17, 48, LCD_PIXEL_BLACK);

  if (valid && t != nullptr) {
    char line[24];
    snprintf(line, sizeof(line), "%02u:%02u:%02u",
             t->hour, t->minute, t->second);
    drawCenteredText(line, 78, 4);

    snprintf(line, sizeof(line), "%04u-%02u-%02u",
             t->year, t->month, t->day);
    drawCenteredText(line, 138, 2);
    drawCenteredText(kWeekdayNames[t->weekday % 7], 178, 2);
  } else {
    drawCenteredText("RTC TIME", 82, 3);
    drawCenteredText("UNAVAILABLE", 126, 3);
    drawCenteredText("SET VIA SERIAL", 184, 2);
  }

  if (status != nullptr && status[0] != '\0') {
    drawCenteredText(status, 240, 1);
  }
  drawCenteredText("SERIAL: YYYY-MM-DD HH:MM:SS", 270, 1);
  panel.flush();
}

// Manual time-setting entry: parse date/time and calculate the weekday, so users need not enter it.
bool parseTimeLine(const String &line, RtcDateTime *out) {
  int y, mo, d, h, mi, s;
  // sscanf is available in Arduino; non-digit literals separate fields in the format.
  if (sscanf(line.c_str(), "%d-%d-%d %d:%d:%d",
             &y, &mo, &d, &h, &mi, &s) != 6) return false;
  out->year = y; out->month = mo; out->day = d;
  out->hour = h; out->minute = mi; out->second = s;
  if (y < 2000 || y > 2099 || mo < 1 || mo > 12 ||
      d < 1 || h < 0 || mi < 0 || s < 0 ||
      h > 23 || mi > 59 || s > 59) return false;
  out->weekday = calculateWeekday(out->year, out->month, out->day);
  return isValidDateTime(*out);
}

// Collect one Serial input line without blocking; return true when a newline arrives.
bool readSerialLine(String *lineOut) {
  static String pending;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      *lineOut = pending;
      pending = "";
      return true;
    }
    pending += ch;
    if (pending.length() > 40) pending = "";   // Guard against abnormally long input.
  }
  return false;
}

// Set time only after receiving a complete line; read back after writing. An I2C ACK alone does not establish trusted time.
void handleTimeInput() {
  String line;
  if (readSerialLine(&line)) {
    RtcDateTime t;
    if (parseTimeLine(line, &t)) {
      if (rtc.write(t)) {
        RtcDateTime verify;
        bool osFlag = true;
        if (rtc.read(&verify, &osFlag) && !osFlag && isValidDateTime(verify)) {
          timeLooksValid = true;
          nextRtcReadMs = millis() + 1000;
          Serial.print(F("RTC set OK -> "));
          printTime(verify, false);
          drawRtcScreen(&verify, true, "RTC SET OK");
        } else {
          timeLooksValid = false;
          Serial.println(F("RTC write completed, but read-back validation FAILED"));
          drawRtcScreen(nullptr, false, "RTC VERIFY FAILED");
        }
      } else {
        Serial.println(F("RTC write FAILED"));
      }
    } else {
      Serial.println(F("Bad format. Example: 2026-08-26 12:34:56"));
    }
  }
}

void refreshRtcTime() {
  // The deadline check tolerates millis() wraparound; this second only paces reads/display updates, while the RTC keeps time independently.
  if (timeLooksValid && (int32_t)(millis() - nextRtcReadMs) >= 0) {
    nextRtcReadMs += 1000;
    RtcDateTime now;
    bool osFlag = false;
    if (rtc.read(&now, &osFlag) && !osFlag && isValidDateTime(now)) {
      printTime(now, false);
      drawRtcScreen(&now, true, "UPDATED EACH SECOND");
    } else {
      timeLooksValid = false;
      Serial.println(F("RTC became invalid; periodic output paused."));
      Serial.println(F("Send YYYY-MM-DD HH:MM:SS to set it again."));
      drawRtcScreen(nullptr, false,
                    osFlag ? "OS FLAG: SET RTC TIME" : "RTC READ FAILED");
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("[05] PCF85063 RTC"));

  panelReady = panel.begin();
  Serial.println(panelReady ? F("PANEL_INIT: PASS") : F("PANEL_INIT: FAIL"));

  if (!i2c.begin(PIN_I2C_SDA, PIN_I2C_SCL)) {
    Serial.println(F("I2C: init FAIL"));
    drawRtcScreen(nullptr, false, "I2C INIT FAILED");
    while (true) delay(1000);
  }
  if (!rtc.begin(i2c)) {
    Serial.println(F("RTC: attach FAIL (check wiring/address 0x51)"));
    drawRtcScreen(nullptr, false, "RTC ATTACH FAILED");
    while (true) delay(1000);
  }

  RtcDateTime now;
  bool osFlag = false;
  if (rtc.read(&now, &osFlag)) {
    timeLooksValid = !osFlag && isValidDateTime(now);

    if (timeLooksValid) {
      Serial.print(F("RTC time -> "));
      printTime(now, false);
      drawRtcScreen(&now, true, "UPDATED EACH SECOND");
    } else {
      Serial.print(F("RTC returned an untrusted value -> "));
      printTime(now, osFlag);
      drawRtcScreen(nullptr, false,
                    osFlag ? "OS FLAG: SET RTC TIME" : "INVALID RTC VALUE");
    }
  } else {
    Serial.println(F("RTC: first read FAIL"));
    drawRtcScreen(nullptr, false, "RTC READ FAILED");
  }

  if (!timeLooksValid) {
    Serial.println(F("RTC time unavailable. Send a line like:"));
    Serial.println(F("  2026-08-28 12:34:56"));
  } else {
    Serial.println(F("Send YYYY-MM-DD HH:MM:SS at any time to correct it."));
  }
  nextRtcReadMs = millis() + 1000;
}

void loop() {
  handleTimeInput();
  refreshRtcTime();
  delay(5);
}
