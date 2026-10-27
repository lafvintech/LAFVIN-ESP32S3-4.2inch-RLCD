#pragma once

#include <stdint.h>

class RlcdPanel;   // Forward declaration; drawing functions act on the panel's framebuffer.

// The origin is top-left, x points right and y down; rectangle w/h are sizes, not bottom-right coordinates.
// Drawing only modifies the buffer; the caller flushes once after drawing the whole page.

// ---- Basic shapes (classic algorithms; see comments in draw_bsp.cpp) ----
void drawLine(RlcdPanel &panel, int16_t x0, int16_t y0,
              int16_t x1, int16_t y1, uint8_t color);
void drawRect(RlcdPanel &panel, int16_t x, int16_t y,
              int16_t w, int16_t h, uint8_t color);
void fillRect(RlcdPanel &panel, int16_t x, int16_t y,
              int16_t w, int16_t h, uint8_t color);
void drawCircle(RlcdPanel &panel, int16_t cx, int16_t cy,
                int16_t r, uint8_t color);
void fillCircle(RlcdPanel &panel, int16_t cx, int16_t cy,
                int16_t r, uint8_t color);

// ---- Text rendering ----
// Printable ASCII only; scale>=1: 1 gives 8x8 pixels, 2 gives 16x16.
// Transparent rendering writes glyph pixels only; clear the old area before updating text, or clear the whole page as here.
void drawChar(RlcdPanel &panel, int16_t x, int16_t y,
              char ch, uint8_t color, uint8_t scale = 1);
void drawString(RlcdPanel &panel, int16_t x, int16_t y,
                const char *text, uint8_t color, uint8_t scale = 1);

// Measure text width: character count x 8 x scale
int16_t textWidth(const char *text, uint8_t scale);

