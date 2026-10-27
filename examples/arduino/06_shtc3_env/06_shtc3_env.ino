// ============================================================
// 06_shtc3_env -- SHTC3 temperature and humidity sensor
//
// Reading order: setup() initializes -> loop() samples periodically -> shtc3_bsp.cpp sends/receives commands.
// Sampling and display run in the same execution flow; no background task is needed.
// Key concepts:
//   * Communication uses 16-bit command codes, not conventional register addresses.
//   * Wake/sleep power saving: sleep when idle, wake before measurement, then return to sleep.
//   * CRC-8 matters: long I2C wires or interference can corrupt bits.
//     CRC rejects bad data instead of displaying an implausible temperature.
//
// Output: sample every 5 seconds; print temperature/humidity to Serial and show them on the RLCD.
//         On measurement failure, both outputs show the error type.
// ============================================================

#include <Arduino.h>
#include "board_pins.h"
#include "display_bsp.h"
#include "draw_bsp.h"
#include "i2c_bus_bsp.h"
#include "shtc3_bsp.h"

namespace {
RlcdPanel panel(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC,
                PIN_LCD_CS, PIN_LCD_RST,
                LCD_WIDTH_LANDSCAPE, LCD_HEIGHT_LANDSCAPE);
I2cBus i2c;
Shtc3Sensor shtc3;
bool panelReady = false;
bool chipIdValid = false;
uint16_t chipId = 0;

const char *errName(Shtc3Error e) {
  switch (e) {
    case SHTC3_OK: return "OK";
    case SHTC3_ERR_I2C: return "I2C";
    case SHTC3_ERR_CRC: return "CRC";
  }
  return "?";
}

void drawCenteredText(const char *text, int16_t y, uint8_t scale) {
  const int16_t x = (panel.width() - textWidth(text, scale)) / 2;
  drawString(panel, x, y, text, LCD_PIXEL_BLACK, scale);
}

void drawStatusScreen(const char *message) {
  if (!panelReady) return;

  panel.clearBuffer(LCD_PIXEL_WHITE);
  drawString(panel, 16, 16, "SHTC3 ENV", LCD_PIXEL_BLACK, 2);
  drawLine(panel, 16, 48, panel.width() - 17, 48, LCD_PIXEL_BLACK);
  drawCenteredText(message, 122, 2);
  panel.flush();
}

void drawEnvironmentScreen(const Shtc3Sample *sample, Shtc3Error err) {
  if (!panelReady) return;

  panel.clearBuffer(LCD_PIXEL_WHITE);
  drawString(panel, 16, 16, "SHTC3 ENV", LCD_PIXEL_BLACK, 2);

  char line[24];
  if (chipIdValid) {
    snprintf(line, sizeof(line), "ID 0x%04X", chipId);
    drawString(panel, panel.width() - textWidth(line, 1) - 16, 24,
               line, LCD_PIXEL_BLACK, 1);
  }
  drawLine(panel, 16, 48, panel.width() - 17, 48, LCD_PIXEL_BLACK);

  if (sample != nullptr && err == SHTC3_OK) {
    drawCenteredText("TEMPERATURE", 66, 2);
    snprintf(line, sizeof(line), "%.2f C", (double)sample->temperatureC);
    drawCenteredText(line, 94, 4);

    drawCenteredText("HUMIDITY", 158, 2);
    snprintf(line, sizeof(line), "%.2f %%", (double)sample->humidityPct);
    drawCenteredText(line, 186, 4);
  } else {
    drawCenteredText("MEASURE FAILED", 100, 3);
    snprintf(line, sizeof(line), "ERROR: %s", errName(err));
    drawCenteredText(line, 158, 2);
  }

  drawCenteredText("REFRESH: 5 SECONDS", 270, 1);
  panel.flush();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("[06] SHTC3 environment sensor"));

  panelReady = panel.begin();
  Serial.println(panelReady ? F("PANEL_INIT: PASS") : F("PANEL_INIT: FAIL"));

  if (!i2c.begin(PIN_I2C_SDA, PIN_I2C_SCL)) {
    Serial.println(F("I2C: init FAIL"));
    drawStatusScreen("I2C INIT FAILED");
    while (true) delay(1000);
  }
  if (!shtc3.begin(i2c)) {
    Serial.println(F("SHTC3: attach FAIL"));
    drawStatusScreen("SHTC3 ATTACH FAILED");
    while (true) delay(1000);
  }

  if (shtc3.readId(&chipId)) {
    // This only confirms that ID data was read and passed CRC; the driver does not compare the product-code bits.
    chipIdValid = true;
    Serial.printf("chip ID: 0x%04X\n", chipId);
  } else {
    Serial.println(F("read ID FAILED (wiring? address 0x70?)"));
  }

  drawStatusScreen("WAITING FOR SAMPLE");
}

void loop() {
  static uint32_t nextRunMs = 0;
  const uint32_t kMeasureIntervalMs = 5000;

  const uint32_t now = millis();
  if ((int32_t)(now - nextRunMs) < 0) {
    delay(20);
    return;
  }
  nextRunMs = now + kMeasureIntervalMs;

  // measure() waits synchronously; use sample only on success and display the error otherwise.
  Shtc3Sample sample;
  Shtc3Error err = shtc3.measure(&sample);
  if (err == SHTC3_OK) {
    Serial.printf("T = %.2f C, RH = %.2f %%\n",
                  sample.temperatureC, sample.humidityPct);
    drawEnvironmentScreen(&sample, err);
  } else {
    Serial.printf("measure FAILED (%s)\n", errName(err));
    drawEnvironmentScreen(nullptr, err);
  }
}
