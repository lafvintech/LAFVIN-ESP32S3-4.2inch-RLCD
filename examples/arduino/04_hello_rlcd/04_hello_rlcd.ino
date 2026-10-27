// ============================================================
// 04_hello_rlcd -- From pixels to UI: hand-written drawing primitives
//
// This chapter introduces no third-party graphics library (including LVGL):
//   draw_bsp.cpp implements lines, rectangles, circles, and bitmap text by hand.
//   Pixels still go through RlcdPanel.setPixel() from chapter 03.
//
// Screen: inverted title + geometry card + font card + progress bar,
// demonstrating how basic primitives compose a complete static UI.
// Reading path: setup() -> drawGraphicsLab() -> individual cards -> draw_bsp.cpp -> setPixel().
// This page has a fixed 400x300 layout; changing orientation also requires updating coordinates.
// ============================================================

#include <Arduino.h>
#include "board_pins.h"
#include "display_bsp.h"
#include "draw_bsp.h"

namespace {
RlcdPanel panel(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC,
                PIN_LCD_CS, PIN_LCD_RST,
                LCD_WIDTH_LANDSCAPE, LCD_HEIGHT_LANDSCAPE);

constexpr uint8_t kProgressValue = 72;

void drawHeader() {
  // Draw a black background, then white text for an inverted title.
  fillRect(panel, 12, 12, 376, 36, LCD_PIXEL_BLACK);
  drawString(panel, 24, 26, "04 PIXEL STUDIO", LCD_PIXEL_WHITE, 2);
  drawString(panel, 320, 26, "400X300", LCD_PIXEL_WHITE, 1);
}

void drawGeometryCard() {
  drawRect(panel, 16, 72, 176, 146, LCD_PIXEL_BLACK);
  drawString(panel, 24, 82, "GEOMETRY", LCD_PIXEL_BLACK, 1);
  drawLine(panel, 24, 100, 184, 100, LCD_PIXEL_BLACK);

  const int16_t cx = 104;
  const int16_t cy = 151;

  // Overlay lines, circles, and filled dots in the same coordinate space to form a geometric target.
  drawLine(panel, cx - 50, cy, cx + 50, cy, LCD_PIXEL_BLACK);
  drawLine(panel, cx, cy - 46, cx, cy + 46, LCD_PIXEL_BLACK);
  drawLine(panel, cx - 27, cy - 27, cx + 27, cy + 27, LCD_PIXEL_BLACK);
  drawLine(panel, cx - 27, cy + 27, cx + 27, cy - 27, LCD_PIXEL_BLACK);
  drawCircle(panel, cx, cy, 36, LCD_PIXEL_BLACK);
  drawCircle(panel, cx, cy, 20, LCD_PIXEL_BLACK);
  fillCircle(panel, cx, cy, 5, LCD_PIXEL_BLACK);

  drawString(panel, 44, 204, "LINE / CIRCLE", LCD_PIXEL_BLACK, 1);
}

void drawTypographyCard() {
  drawRect(panel, 208, 72, 176, 146, LCD_PIXEL_BLACK);
  drawString(panel, 216, 82, "TYPE / SCALE", LCD_PIXEL_BLACK, 1);
  drawLine(panel, 216, 100, 376, 100, LCD_PIXEL_BLACK);

  drawString(panel, 220, 110, "1X  ABC 123", LCD_PIXEL_BLACK, 1);
  drawString(panel, 220, 130, "2X HELLO", LCD_PIXEL_BLACK, 2);
  drawString(panel, 220, 164, "3X UI", LCD_PIXEL_BLACK, 3);

  // Inverted labels on the left, outlined labels on the right.
  fillRect(panel, 218, 198, 56, 16, LCD_PIXEL_BLACK);
  drawString(panel, 234, 202, "INV", LCD_PIXEL_WHITE, 1);
  drawRect(panel, 286, 198, 88, 16, LCD_PIXEL_BLACK);
  drawString(panel, 302, 202, "OUTLINE", LCD_PIXEL_BLACK, 1);
}

void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h,
                     uint8_t value) {
  if (value > 100) value = 100;

  drawRect(panel, x, y, w, h, LCD_PIXEL_BLACK);
  const int16_t innerWidth = w - 4;
  const int16_t fillWidth =
      (int16_t)((int32_t)innerWidth * value / 100);
  fillRect(panel, x + 2, y + 2, fillWidth, h - 4, LCD_PIXEL_BLACK);
}

void drawFooter() {
  drawString(panel, 24, 232, "FROM PIXEL TO UI", LCD_PIXEL_BLACK, 1);

  char valueText[8];
  snprintf(valueText, sizeof(valueText), "%u%%", kProgressValue);
  drawString(panel, 264, 232, valueText, LCD_PIXEL_BLACK, 1);
  drawProgressBar(24, 250, 272, 20, kProgressValue);

  drawString(panel, 320, 230, "STATUS", LCD_PIXEL_BLACK, 1);
  drawRect(panel, 312, 244, 64, 26, LCD_PIXEL_BLACK);
  drawString(panel, 324, 253, "READY", LCD_PIXEL_BLACK, 1);
}

void drawGraphicsLab() {
  // Clear the background, then draw the entire page. Drawing functions do not flush, avoiding a full transfer per shape.
  panel.clearBuffer(LCD_PIXEL_WHITE);
  drawRect(panel, 8, 8, 384, 284, LCD_PIXEL_BLACK);
  drawHeader();
  drawGeometryCard();
  drawTypographyCard();
  drawFooter();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("[04] pixel studio"));

  if (!panel.begin()) {
    Serial.println(F("PANEL_INIT: FAIL"));
    while (true) delay(1000);
  }

  uint32_t t0 = millis();
  drawGraphicsLab();
  uint32_t drawMs = millis() - t0;

  t0 = millis();
  panel.flush();
  // The Arduino driver only submits the transfer; SPI may not have finished yet.
  uint32_t flushMs = millis() - t0;

  Serial.printf("draw page : %lu ms\n", (unsigned long)drawMs);
  Serial.printf("flush     : %lu ms\n", (unsigned long)flushMs);
}

void loop() {
  delay(1000);   // Static image
}
