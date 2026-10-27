/* 03 Display self-test: app_main initializes, animates a checkerboard, draws asymmetric orientation marks, then returns.
  * Call board_display's pixel API directly to inspect the driver without a graphics/UI layer.
  * Pixel packing, SPI transfers, and register sequences are in components/board_display. */
#include <inttypes.h>
#include <stdint.h>

#include "board_display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CHECKER_CELL_SIZE 20
#define CELL_DELAY_MS     80

static const char *TAG = "rlcd_driver";

static void fill_rectangle(
    int16_t x, int16_t y, int16_t width, int16_t height,
    board_display_color_t color)
{
    for (int16_t row = 0; row < height; ++row) {
        for (int16_t column = 0; column < width; ++column) {
            board_display_set_pixel(x + column, y + row, color);
        }
    }
}

static void fill_checker_cell(int16_t x, int16_t y)
{
    const int16_t x_end = x + CHECKER_CELL_SIZE < board_display_width()
                              ? x + CHECKER_CELL_SIZE
                              : board_display_width();
    const int16_t y_end = y + CHECKER_CELL_SIZE < board_display_height()
                              ? y + CHECKER_CELL_SIZE
                              : board_display_height();

    for (int16_t pixel_y = y; pixel_y < y_end; ++pixel_y) {
        for (int16_t pixel_x = x; pixel_x < x_end; ++pixel_x) {
            board_display_set_pixel(
                pixel_x, pixel_y, BOARD_DISPLAY_BLACK);
        }
    }
}

static esp_err_t animate_checkerboard(uint16_t *lit_cells)
{
    if (lit_cells == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *lit_cells = 0;

    board_display_clear(BOARD_DISPLAY_WHITE);
    esp_err_t err = board_display_flush();
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(CELL_DELAY_MS));

    const int16_t columns =
        (board_display_width() + CHECKER_CELL_SIZE - 1) /
        CHECKER_CELL_SIZE;
    const int16_t rows =
        (board_display_height() + CHECKER_CELL_SIZE - 1) /
        CHECKER_CELL_SIZE;

    for (int16_t row = 0; row < rows; ++row) {
        for (int16_t column = 0; column < columns; ++column) {
            if (((row + column) & 1) == 0) {
                continue;
            }

            fill_checker_cell(
                column * CHECKER_CELL_SIZE, row * CHECKER_CELL_SIZE);
            /* Add just one black square in memory, but still transfer the full frame, not a partial update. */
            err = board_display_flush();
            if (err != ESP_OK) {
                return err;
            }
            ++(*lit_cells);
            vTaskDelay(pdMS_TO_TICKS(CELL_DELAY_MS));
        }
    }
    return ESP_OK;
}

static void draw_orientation_markers(void)
{
    const int16_t width = board_display_width();
    const int16_t height = board_display_height();
    const int16_t thickness = 6;
    const int16_t top_right_arm = 48;
    /* Unequal L arms avoid 180-degree symmetry, making an upside-down image distinguishable. */
    const int16_t bottom_left_arm = 64;
    const int16_t plus_size = 46;

    board_display_clear(BOARD_DISPLAY_WHITE);

    fill_rectangle(
        width - top_right_arm, 0,
        top_right_arm, thickness, BOARD_DISPLAY_BLACK);
    fill_rectangle(
        width - thickness, 0,
        thickness, top_right_arm, BOARD_DISPLAY_BLACK);

    fill_rectangle(
        0, height - bottom_left_arm,
        thickness, bottom_left_arm, BOARD_DISPLAY_BLACK);
    fill_rectangle(
        0, height - thickness,
        bottom_left_arm, thickness, BOARD_DISPLAY_BLACK);

    const int16_t plus_x = (width - plus_size) / 2;
    const int16_t plus_y = (height - plus_size) / 2;
    fill_rectangle(
        (width - thickness) / 2, plus_y,
        thickness, plus_size, BOARD_DISPLAY_BLACK);
    fill_rectangle(
        plus_x, (height - thickness) / 2,
        plus_size, thickness, BOARD_DISPLAY_BLACK);
}

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "[03] ST7305 RLCD driver/self-test");

    const board_display_config_t display_config =
        BOARD_DISPLAY_CONFIG_DEFAULT();
    int64_t started_at = esp_timer_get_time();
    esp_err_t err = board_display_init(&display_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PANEL_INIT: FAIL (%s)", esp_err_to_name(err));
        return;
    }
    const uint32_t init_ms =
        (uint32_t)((esp_timer_get_time() - started_at) / 1000);

    uint16_t lit_cells = 0;
    started_at = esp_timer_get_time();
    err = animate_checkerboard(&lit_cells);
    const uint32_t animation_ms =
        (uint32_t)((esp_timer_get_time() - started_at) / 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "checker animation failed: %s", esp_err_to_name(err));
        return;
    }

    draw_orientation_markers();
    err = board_display_flush();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "final frame failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "panel init    : %" PRIu32 " ms", init_ms);
    ESP_LOGI(TAG, "checker cells : %u black cells", (unsigned)lit_cells);
    ESP_LOGI(TAG, "animation     : %" PRIu32 " ms", animation_ms);
    ESP_LOGI(TAG, "markers       : TR=short L, BL=long L, center=plus");
    ESP_LOGI(TAG, "frame buffer  : %u bytes (SPIRAM)",
             (unsigned)board_display_frame_buffer_size());
    ESP_LOGI(TAG, "PANEL_INIT: PASS");
    /* No idle loop is needed; the controller retains the last frame without app_main keeping it alive. */
}
