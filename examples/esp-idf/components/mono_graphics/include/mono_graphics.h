#pragma once

#include <stdint.h>

#include "board_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal monochrome drawing layer; it does not manage SPI, tasks, or framebuffer lifetimes.
  * Initialize board_display first; this layer only writes its buffer, and the application flushes after drawing the whole page.
  * Origin is top-left, with x rightward and y downward; rectangle width/height are dimensions.
  * For multi-task use, callers must protect the entire clear -> draw -> flush sequence. */
void mono_gfx_draw_line(
    int16_t x0, int16_t y0, int16_t x1, int16_t y1,
    board_display_color_t color);
void mono_gfx_draw_rect(
    int16_t x, int16_t y, int16_t width, int16_t height,
    board_display_color_t color);
void mono_gfx_fill_rect(
    int16_t x, int16_t y, int16_t width, int16_t height,
    board_display_color_t color);
void mono_gfx_draw_circle(
    int16_t center_x, int16_t center_y, int16_t radius,
    board_display_color_t color);
void mono_gfx_fill_circle(
    int16_t center_x, int16_t center_y, int16_t radius,
    board_display_color_t color);

/* 8x8 printable ASCII, scale>=1; only glyph pixels are written and the background is not erased.
  * Clear the old area before updating text; UTF-8 Chinese is unsupported. Do not copy font data directly to the panel buffer. */
void mono_gfx_draw_char(
    int16_t x, int16_t y, char character,
    board_display_color_t color, uint8_t scale);
void mono_gfx_draw_text(
    int16_t x, int16_t y, const char *text,
    board_display_color_t color, uint8_t scale);
int16_t mono_gfx_text_width(const char *text, uint8_t scale);

#ifdef __cplusplus
}
#endif
