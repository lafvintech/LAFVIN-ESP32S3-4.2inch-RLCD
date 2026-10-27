/* 04 From pixels to UI: app_main -> draw_graphics_lab -> individual cards -> mono_graphics.
  * mono_graphics converts graphics/text to pixels; board_display handles packing and SPI.
  * The layout is fixed at 400x300; drawing only updates the framebuffer, followed by one flush for the complete page. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "board_display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mono_graphics.h"

#define PROGRESS_VALUE 72

static const char *TAG = "hello_rlcd";

static void draw_header(void)
{
    /* Text rendering is transparent: only glyph pixels are written. Draw a black background before a white title. */
    mono_gfx_fill_rect(12, 12, 376, 36, BOARD_DISPLAY_BLACK);
    mono_gfx_draw_text(
        24, 26, "04 PIXEL STUDIO", BOARD_DISPLAY_WHITE, 2);
    mono_gfx_draw_text(
        320, 26, "400X300", BOARD_DISPLAY_WHITE, 1);
}

static void draw_geometry_card(void)
{
    mono_gfx_draw_rect(16, 72, 176, 146, BOARD_DISPLAY_BLACK);
    mono_gfx_draw_text(24, 82, "GEOMETRY", BOARD_DISPLAY_BLACK, 1);
    mono_gfx_draw_line(24, 100, 184, 100, BOARD_DISPLAY_BLACK);

    const int16_t center_x = 104;
    const int16_t center_y = 151;
    mono_gfx_draw_line(
        center_x - 50, center_y, center_x + 50, center_y,
        BOARD_DISPLAY_BLACK);
    mono_gfx_draw_line(
        center_x, center_y - 46, center_x, center_y + 46,
        BOARD_DISPLAY_BLACK);
    mono_gfx_draw_line(
        center_x - 27, center_y - 27, center_x + 27, center_y + 27,
        BOARD_DISPLAY_BLACK);
    mono_gfx_draw_line(
        center_x - 27, center_y + 27, center_x + 27, center_y - 27,
        BOARD_DISPLAY_BLACK);
    mono_gfx_draw_circle(center_x, center_y, 36, BOARD_DISPLAY_BLACK);
    mono_gfx_draw_circle(center_x, center_y, 20, BOARD_DISPLAY_BLACK);
    mono_gfx_fill_circle(center_x, center_y, 5, BOARD_DISPLAY_BLACK);

    mono_gfx_draw_text(
        44, 204, "LINE / CIRCLE", BOARD_DISPLAY_BLACK, 1);
}

static void draw_typography_card(void)
{
    mono_gfx_draw_rect(208, 72, 176, 146, BOARD_DISPLAY_BLACK);
    mono_gfx_draw_text(
        216, 82, "TYPE / SCALE", BOARD_DISPLAY_BLACK, 1);
    mono_gfx_draw_line(216, 100, 376, 100, BOARD_DISPLAY_BLACK);

    mono_gfx_draw_text(220, 110, "1X  ABC 123", BOARD_DISPLAY_BLACK, 1);
    mono_gfx_draw_text(220, 130, "2X HELLO", BOARD_DISPLAY_BLACK, 2);
    mono_gfx_draw_text(220, 164, "3X UI", BOARD_DISPLAY_BLACK, 3);

    mono_gfx_fill_rect(218, 198, 56, 16, BOARD_DISPLAY_BLACK);
    mono_gfx_draw_text(234, 202, "INV", BOARD_DISPLAY_WHITE, 1);
    mono_gfx_draw_rect(286, 198, 88, 16, BOARD_DISPLAY_BLACK);
    mono_gfx_draw_text(302, 202, "OUTLINE", BOARD_DISPLAY_BLACK, 1);
}

static void draw_progress_bar(
    int16_t x, int16_t y, int16_t width, int16_t height, uint8_t value)
{
    if (value > 100) {
        value = 100;
    }

    mono_gfx_draw_rect(x, y, width, height, BOARD_DISPLAY_BLACK);
    const int16_t inner_width = width - 4;
    const int16_t fill_width =
        (int16_t)((int32_t)inner_width * value / 100);
    mono_gfx_fill_rect(
        x + 2, y + 2, fill_width, height - 4, BOARD_DISPLAY_BLACK);
}

static void draw_footer(void)
{
    mono_gfx_draw_text(
        24, 232, "FROM PIXEL TO UI", BOARD_DISPLAY_BLACK, 1);

    char value_text[8];
    snprintf(value_text, sizeof(value_text), "%u%%", (unsigned)PROGRESS_VALUE);
    mono_gfx_draw_text(264, 232, value_text, BOARD_DISPLAY_BLACK, 1);
    draw_progress_bar(24, 250, 272, 20, PROGRESS_VALUE);

    mono_gfx_draw_text(320, 230, "STATUS", BOARD_DISPLAY_BLACK, 1);
    mono_gfx_draw_rect(312, 244, 64, 26, BOARD_DISPLAY_BLACK);
    mono_gfx_draw_text(324, 253, "READY", BOARD_DISPLAY_BLACK, 1);
}

static void draw_graphics_lab(void)
{
    board_display_clear(BOARD_DISPLAY_WHITE);
    mono_gfx_draw_rect(8, 8, 384, 284, BOARD_DISPLAY_BLACK);
    draw_header();
    draw_geometry_card();
    draw_typography_card();
    draw_footer();
}

void app_main(void)
{
    ESP_LOGI(TAG, "[04] pixel studio");

    const board_display_config_t display_config =
        BOARD_DISPLAY_CONFIG_DEFAULT();
    esp_err_t err = board_display_init(&display_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PANEL_INIT: FAIL (%s)", esp_err_to_name(err));
        return;
    }

    int64_t started_at = esp_timer_get_time();
    draw_graphics_lab();
    const uint32_t draw_ms =
        (uint32_t)((esp_timer_get_time() - started_at) / 1000);

    started_at = esp_timer_get_time();
    /* Unlike the Arduino version, the IDF driver waits for transfer completion, so this timing includes SPI transmission. */
    err = board_display_flush();
    const uint32_t flush_ms =
        (uint32_t)((esp_timer_get_time() - started_at) / 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display flush failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "draw page : %" PRIu32 " ms", draw_ms);
    ESP_LOGI(TAG, "flush     : %" PRIu32 " ms", flush_ms);
}
