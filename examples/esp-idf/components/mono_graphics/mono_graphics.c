#include "mono_graphics.h"

#include <stddef.h>
#include <string.h>

#include "font8x8.h"

void mono_gfx_draw_line(
    int16_t x0, int16_t y0, int16_t x1, int16_t y1,
    board_display_color_t color)
{
    /* Bresenham: accumulate error to choose step directions using integers only; include both endpoints. */
    int32_t current_x = x0;
    int32_t current_y = y0;
    const int32_t delta_x = x1 >= x0 ? x1 - x0 : x0 - x1;
    const int32_t step_x = x0 < x1 ? 1 : -1;
    const int32_t delta_y = y1 >= y0 ? y0 - y1 : y1 - y0;
    const int32_t step_y = y0 < y1 ? 1 : -1;
    int32_t error = delta_x + delta_y;

    for (;;) {
        board_display_set_pixel(
            (int16_t)current_x, (int16_t)current_y, color);
        if (current_x == x1 && current_y == y1) {
            break;
        }

        const int32_t doubled_error = error * 2;
        if (doubled_error >= delta_y) {
            error += delta_y;
            current_x += step_x;
        }
        if (doubled_error <= delta_x) {
            error += delta_x;
            current_y += step_y;
        }
    }
}

void mono_gfx_draw_rect(
    int16_t x, int16_t y, int16_t width, int16_t height,
    board_display_color_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    mono_gfx_draw_line(x, y, x + width - 1, y, color);
    mono_gfx_draw_line(
        x, y + height - 1, x + width - 1, y + height - 1, color);
    mono_gfx_draw_line(x, y, x, y + height - 1, color);
    mono_gfx_draw_line(
        x + width - 1, y, x + width - 1, y + height - 1, color);
}

void mono_gfx_fill_rect(
    int16_t x, int16_t y, int16_t width, int16_t height,
    board_display_color_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    for (int16_t row = 0; row < height; ++row) {
        for (int16_t column = 0; column < width; ++column) {
            board_display_set_pixel(x + column, y + row, color);
        }
    }
}

void mono_gfx_draw_circle(
    int16_t center_x, int16_t center_y, int16_t radius,
    board_display_color_t color)
{
    if (radius < 0) {
        return;
    }

    /* Midpoint circle: compute one octant, then mirror it to obtain the remaining pixels. */
    int32_t x = 0;
    int32_t y = radius;
    int32_t decision = 1 - radius;

    while (x <= y) {
        board_display_set_pixel(center_x + x, center_y + y, color);
        board_display_set_pixel(center_x - x, center_y + y, color);
        board_display_set_pixel(center_x + x, center_y - y, color);
        board_display_set_pixel(center_x - x, center_y - y, color);
        board_display_set_pixel(center_x + y, center_y + x, color);
        board_display_set_pixel(center_x - y, center_y + x, color);
        board_display_set_pixel(center_x + y, center_y - x, color);
        board_display_set_pixel(center_x - y, center_y - x, color);

        if (decision < 0) {
            decision += 2 * x + 3;
        } else {
            decision += 2 * (x - y) + 5;
            --y;
        }
        ++x;
    }
}

static void draw_horizontal_line(
    int32_t x0, int32_t x1, int32_t y, board_display_color_t color)
{
    mono_gfx_draw_line(
        (int16_t)x0, (int16_t)y, (int16_t)x1, (int16_t)y, color);
}

void mono_gfx_fill_circle(
    int16_t center_x, int16_t center_y, int16_t radius,
    board_display_color_t color)
{
    if (radius < 0) {
        return;
    }

    /* Use the same circle boundary calculation and fill with symmetric horizontal spans, without floating-point square roots. */
    int32_t x = 0;
    int32_t y = radius;
    int32_t decision = 1 - radius;

    while (x <= y) {
        draw_horizontal_line(
            center_x - x, center_x + x, center_y + y, color);
        draw_horizontal_line(
            center_x - x, center_x + x, center_y - y, color);
        draw_horizontal_line(
            center_x - y, center_x + y, center_y + x, color);
        draw_horizontal_line(
            center_x - y, center_x + y, center_y - x, color);

        if (decision < 0) {
            decision += 2 * x + 3;
        } else {
            decision += 2 * (x - y) + 5;
            --y;
        }
        ++x;
    }
}

void mono_gfx_draw_char(
    int16_t x, int16_t y, char character,
    board_display_color_t color, uint8_t scale)
{
    if (scale == 0) {
        return;
    }

    uint8_t code = (uint8_t)character;
    if (code < FONT8X8_FIRST_CHAR || code > FONT8X8_LAST_CHAR) {
        code = '?';
    }
    const uint8_t *glyph = s_font8x8[code - FONT8X8_FIRST_CHAR];

    for (uint8_t row = 0; row < 8; ++row) {
        for (uint8_t column = 0; column < 8; ++column) {
            /* Font bit 0 is leftmost; write logical pixels only and let board_display handle panel bit order. */
            if ((glyph[row] & (1U << column)) == 0) {
                continue;
            }
            if (scale == 1) {
                board_display_set_pixel(x + column, y + row, color);
            } else {
                mono_gfx_fill_rect(
                    x + column * scale, y + row * scale,
                    scale, scale, color);
            }
        }
    }
}

void mono_gfx_draw_text(
    int16_t x, int16_t y, const char *text,
    board_display_color_t color, uint8_t scale)
{
    if (text == NULL || scale == 0) {
        return;
    }

    int16_t cursor_x = x;
    while (*text != '\0') {
        mono_gfx_draw_char(cursor_x, y, *text, color, scale);
        cursor_x += 8 * scale;
        ++text;
    }
}

int16_t mono_gfx_text_width(const char *text, uint8_t scale)
{
    if (text == NULL || scale == 0) {
        return 0;
    }
    return (int16_t)(strlen(text) * 8 * scale);
}
