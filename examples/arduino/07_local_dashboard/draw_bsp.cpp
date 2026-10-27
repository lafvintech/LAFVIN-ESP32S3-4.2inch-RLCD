#include <Arduino.h>
#include "display_bsp.h"
#include "font8x8.h"
#include "draw_bsp.h"

// ============================================================
// Minimal drawing primitives, all using classic computer graphics algorithms:
//   Lines -- Bresenham: integer addition/subtraction only, no floating point or division.
//   Circles -- midpoint circle: use eight-way symmetry to calculate only 1/8 of the arc.
//   Text -- expand the bitmap font bit by bit.
// The aim is to explain pixels-to-shapes, not to build a full-featured graphics library.
// ============================================================

void drawLine(RlcdPanel &panel, int16_t x0, int16_t y0,
              int16_t x1, int16_t y1, uint8_t color) {
  // Bresenham: the signs of dx/dy determine the step direction;
  // advance along the minor axis when the accumulated error exceeds half a pixel.
  int16_t dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
  int16_t dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
  int16_t err = dx + dy;

  while (true) {
    panel.setPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = err * 2;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void drawRect(RlcdPanel &panel, int16_t x, int16_t y,
              int16_t w, int16_t h, uint8_t color) {
  // Draw one line for each of the four edges.
  drawLine(panel, x, y, x + w - 1, y, color);
  drawLine(panel, x, y + h - 1, x + w - 1, y + h - 1, color);
  drawLine(panel, x, y, x, y + h - 1, color);
  drawLine(panel, x + w - 1, y, x + w - 1, y + h - 1, color);
}

void fillRect(RlcdPanel &panel, int16_t x, int16_t y,
              int16_t w, int16_t h, uint8_t color) {
  for (int16_t row = 0; row < h; row++) {
    for (int16_t col = 0; col < w; col++) {
      panel.setPixel(x + col, y + row, color);
    }
  }
}

void drawCircle(RlcdPanel &panel, int16_t cx, int16_t cy,
                int16_t r, uint8_t color) {
  // Midpoint circle: start at (0, r); the decision value determines whether y decreases when x advances.
  // Eight-way symmetry: plot 8 symmetric points for each calculated (x,y).
  int16_t px = 0, py = r;
  int32_t decision = 1 - r;
  while (px <= py) {
    panel.setPixel(cx + px, cy + py, color);
    panel.setPixel(cx - px, cy + py, color);
    panel.setPixel(cx + px, cy - py, color);
    panel.setPixel(cx - px, cy - py, color);
    panel.setPixel(cx + py, cy + px, color);
    panel.setPixel(cx - py, cy + px, color);
    panel.setPixel(cx + py, cy - px, color);
    panel.setPixel(cx - py, cy - px, color);
    if (decision < 0) {
      decision += 2 * px + 3;
    } else {
      decision += 2 * (px - py) + 5;
      py--;
    }
    px++;
  }
}

void fillCircle(RlcdPanel &panel, int16_t cx, int16_t cy,
                int16_t r, uint8_t color) {
  // Simple approach: draw horizontal spans row by row (width follows the circle equation).
  for (int16_t dy = -r; dy <= r; dy++) {
    int16_t spanX = (int16_t)sqrtf((float)r * r - (float)dy * dy);
    drawLine(panel, cx - spanX, cy + dy, cx + spanX, cy + dy, color);
  }
}

void drawChar(RlcdPanel &panel, int16_t x, int16_t y,
              char ch, uint8_t color, uint8_t scale) {
  if (ch < FONT8X8_FIRST_CHAR || ch > FONT8X8_LAST_CHAR) {
    ch = '?';   // Use a question mark for characters outside the font.
  }
  const unsigned char *glyph = kFont8x8[ch - FONT8X8_FIRST_CHAR];

  for (uint8_t row = 0; row < 8; row++) {
    unsigned char bits = glyph[row];
    for (uint8_t col = 0; col < 8; col++) {
      if (bits & (1u << col)) {             // bit0 is the leftmost pixel.
        if (scale == 1) {
          panel.setPixel(x + col, y + row, color);
        } else {
          fillRect(panel, x + col * scale, y + row * scale,
                   scale, scale, color);    // Scaling: each pixel becomes a scale x scale block.
        }
      }
    }
  }
}

void drawString(RlcdPanel &panel, int16_t x, int16_t y,
                const char *text, uint8_t color, uint8_t scale) {
  int16_t cursor = x;
  while (*text) {
    drawChar(panel, cursor, y, *text, color, scale);
    cursor += 8 * scale;    // Monospaced font: advance by 8*scale for each character.
    text++;
  }
}

int16_t textWidth(const char *text, uint8_t scale) {
  return (int16_t)(strlen(text) * 8 * scale);
}
