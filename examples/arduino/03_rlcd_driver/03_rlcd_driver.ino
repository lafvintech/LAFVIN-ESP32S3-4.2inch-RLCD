// ============================================================
// 03_rlcd_driver -- ST7305 RLCD low-level driver and display self-test
//
// This example:
//   1. Performs full initialization with the custom RlcdPanel driver (no U8g2).
//   2. Displays a white background.
//   3. Turns on black checkerboard cells one by one, row by row, briefly forming a full checkerboard.
//   4. Displays edge-aligned top-right/bottom-left L markers and a centered cross.
//   5. Prints initialization and animation timings to Serial.
//
// Troubleshooting checklist (check in order if the build succeeds but the display is incorrect):
//   a) Was the RESET sequence executed? (hardReset in display_bsp.cpp, 50/20/50 ms)
//   b) Is the SPI clock too high? (10 MHz throughout this course, 24 MHz maximum)
//   c) 0x36 (MADCTL)=0x48 must match setPixel() packing; do not change only the register to test orientation.
//   d) Do not swap RlcdPanel width/height: landscape 400x300 / portrait 300x400.
//      The packing algorithm switches automatically based on width/height.
// See docs/03_rlcd_driver.md and the notes at the top of display_bsp.cpp for details.
// ============================================================

#include <Arduino.h>
#include "board_pins.h"
#include "display_bsp.h"

namespace {
// Landscape 400x300; use (LCD_WIDTH_PORTRAIT, LCD_HEIGHT_PORTRAIT) to try portrait.
RlcdPanel panel(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC,
                PIN_LCD_CS, PIN_LCD_RST,
                LCD_WIDTH_LANDSCAPE, LCD_HEIGHT_LANDSCAPE);

const int16_t kCheckerCell = 20;   // Checkerboard cell side length (pixels)
const uint32_t kCellDelayMs = 80;  // Pause after turning on each black cell
}  // namespace

static void fillCell(int16_t x, int16_t y, int16_t size, uint8_t color) {
  const int16_t cellXEnd = x + size;
  const int16_t cellYEnd = y + size;
  const int16_t xEnd = cellXEnd < panel.width() ? cellXEnd : panel.width();
  const int16_t yEnd = cellYEnd < panel.height() ? cellYEnd : panel.height();
  for (int16_t py = y; py < yEnd; ++py) {
    for (int16_t px = x; px < xEnd; ++px) {
      panel.setPixel(px, py, color);
    }
  }
}

static void fillRectangle(int16_t x, int16_t y,
                          int16_t width, int16_t height,
                          uint8_t color) {
  const int16_t xEnd = x + width;
  const int16_t yEnd = y + height;
  for (int16_t py = y; py < yEnd; ++py) {
    for (int16_t px = x; px < xEnd; ++px) {
      panel.setPixel(px, py, color);
    }
  }
}

static void drawOrientationMarkers() {
  const int16_t w = panel.width();
  const int16_t h = panel.height();
  const int16_t thickness = 6;
  const int16_t topRightArm = 48;
  const int16_t bottomLeftArm = 64;
  const int16_t plusSize = 46;

  // After the checkerboard animation, switch to a separate orientation-test screen without the checkerboard.
  panel.clearBuffer(LCD_PIXEL_WHITE);

  // The two L markers deliberately have different arm lengths; otherwise the top-right/bottom-left
  // pair plus a centered cross would have 180-degree symmetry, hiding an upside-down image.

  // Top-right corner: the horizontal arm touches the top edge, the vertical arm the right edge.
  fillRectangle(w - topRightArm, 0,
                topRightArm, thickness, LCD_PIXEL_BLACK);
  fillRectangle(w - thickness, 0,
                thickness, topRightArm, LCD_PIXEL_BLACK);

  // Bottom-left corner: the vertical arm touches the left edge, the horizontal arm the bottom edge.
  fillRectangle(0, h - bottomLeftArm,
                thickness, bottomLeftArm, LCD_PIXEL_BLACK);
  fillRectangle(0, h - thickness,
                bottomLeftArm, thickness, LCD_PIXEL_BLACK);

  // Centered cross.
  const int16_t plusX = (w - plusSize) / 2;
  const int16_t plusY = (h - plusSize) / 2;
  fillRectangle((w - thickness) / 2, plusY,
                thickness, plusSize, LCD_PIXEL_BLACK);
  fillRectangle(plusX, (h - thickness) / 2,
                plusSize, thickness, LCD_PIXEL_BLACK);
}

static uint16_t animateCheckerboard() {
  const int16_t columns = (panel.width() + kCheckerCell - 1) / kCheckerCell;
  const int16_t rows = (panel.height() + kCheckerCell - 1) / kCheckerCell;
  uint16_t litCells = 0;

  // Flush the clean white background first, then add only one black cell to the framebuffer each time.
  panel.clearBuffer(LCD_PIXEL_WHITE);
  panel.flush();
  delay(kCellDelayMs);

  for (int16_t row = 0; row < rows; ++row) {
    for (int16_t column = 0; column < columns; ++column) {
      // Turn on only black checkerboard cells; white cells remain as the background, yielding a standard checkerboard.
      if (((row + column) & 1) == 0) continue;

      fillCell(column * kCheckerCell, row * kCheckerCell,
               kCheckerCell, LCD_PIXEL_BLACK);
      // Only one cell is added, but each flush() still submits a full frame; the delay makes it visible.
      panel.flush();
      ++litCells;
      delay(kCellDelayMs);
    }
  }

  return litCells;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F("[03] ST7305 RLCD driver/self-test"));

  uint32_t t0 = millis();
  if (!panel.begin()) {
    Serial.println(F("PANEL_INIT: FAIL (frame buffer?)"));
    while (true) delay(1000);
  }
  uint32_t initMs = millis() - t0;
  Serial.printf("panel init     : %lu ms\n", (unsigned long)initMs);

  t0 = millis();
  const uint16_t litCells = animateCheckerboard();
  const uint32_t animationMs = millis() - t0;

  drawOrientationMarkers();
  panel.flush();

  Serial.printf("checker cells  : %u black cells\n", (unsigned)litCells);
  Serial.printf("animation      : %lu ms\n", (unsigned long)animationMs);
  Serial.println(F("markers        : TR=short L, BL=long L, center=plus"));
  Serial.printf("frame buffer   : %u bytes (SPIRAM)\n",
                (unsigned)(panel.width() * panel.height() / 8));
  Serial.println(F("PANEL_INIT: PASS"));
}

void loop() {
  // Play the animation only once at power-on; leave just the two L markers and centered cross on white.
  delay(1000);
}
