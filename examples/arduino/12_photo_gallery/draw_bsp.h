#pragma once

#include <stdint.h>

class RlcdPanel;   // Forward declaration; all drawing functions operate on the panel framebuffer

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
// scale=1 uses native 8x8 size; scale=2 doubles each dimension (each pixel becomes 2x2)
void drawChar(RlcdPanel &panel, int16_t x, int16_t y,
              char ch, uint8_t color, uint8_t scale = 1);
void drawString(RlcdPanel &panel, int16_t x, int16_t y,
                const char *text, uint8_t color, uint8_t scale = 1);

// Text width: character count x 8 x scale
int16_t textWidth(const char *text, uint8_t scale);
