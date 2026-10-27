#pragma once

#include <stdint.h>

class RlcdPanel;   // Forward declaration; drawing functions act on the panel's framebuffer.

// Origin is at the top left, x rightward and y downward; rectangle w/h are sizes, not bottom-right coordinates.
// All functions modify memory only; the caller calls panel.flush() once after drawing the page.
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
// Printable ASCII only; transparent drawing writes only set glyph bits and does not clear the background between characters.
// Clear old text before updating it; this page clears and redraws the entire frame.
// scale>=1: 1 gives 8x8, 2 gives 16x16; drawChar() handles the font bit-order conversion.
void drawChar(RlcdPanel &panel, int16_t x, int16_t y,
              char ch, uint8_t color, uint8_t scale = 1);
void drawString(RlcdPanel &panel, int16_t x, int16_t y,
                const char *text, uint8_t color, uint8_t scale = 1);

// Measure text width: character count x 8 x scale
int16_t textWidth(const char *text, uint8_t scale);

