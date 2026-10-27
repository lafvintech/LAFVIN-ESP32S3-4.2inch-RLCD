/* 06 Environmental measurements: app_main initializes, samples immediately, then samples and draws every 5 seconds.
  * The shtc3 component handles commands, waits, CRC, and conversion; this file only schedules sampling and the page.
  * One task serializes sampling and display updates, so no worker task or display lock is needed. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "board_display.h"
#include "board_i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mono_graphics.h"
#include "shtc3.h"

#define MEASUREMENT_INTERVAL_MS 5000

static const char *TAG = "shtc3_demo";

static bool s_display_ready;
static bool s_chip_id_valid;
static uint16_t s_chip_id;

static const char *measurement_error_name(esp_err_t error)
{
    return error == ESP_ERR_INVALID_CRC ? "CRC" : "I2C";
}

static void draw_centered_text(const char *text, int16_t y, uint8_t scale)
{
    const int16_t x =
        (board_display_width() - mono_gfx_text_width(text, scale)) / 2;
    mono_gfx_draw_text(x, y, text, BOARD_DISPLAY_BLACK, scale);
}

static esp_err_t draw_status_screen(const char *message)
{
    if (!s_display_ready) {
        return ESP_OK;
    }

    board_display_clear(BOARD_DISPLAY_WHITE);
    mono_gfx_draw_text(16, 16, "SHTC3 ENV", BOARD_DISPLAY_BLACK, 2);
    mono_gfx_draw_line(
        16, 48, board_display_width() - 17, 48, BOARD_DISPLAY_BLACK);
    draw_centered_text(message, 122, 2);
    return board_display_flush();
}

static esp_err_t draw_environment_screen(
    const shtc3_sample_t *sample, esp_err_t measurement_error)
{
    if (!s_display_ready) {
        return ESP_OK;
    }

    board_display_clear(BOARD_DISPLAY_WHITE);
    mono_gfx_draw_text(16, 16, "SHTC3 ENV", BOARD_DISPLAY_BLACK, 2);

    char line[24];
    if (s_chip_id_valid) {
        snprintf(line, sizeof(line), "ID 0x%04X", (unsigned)s_chip_id);
        mono_gfx_draw_text(
            board_display_width() - mono_gfx_text_width(line, 1) - 16,
            24, line, BOARD_DISPLAY_BLACK, 1);
    }
    mono_gfx_draw_line(
        16, 48, board_display_width() - 17, 48, BOARD_DISPLAY_BLACK);

    if (sample != NULL && measurement_error == ESP_OK) {
        draw_centered_text("TEMPERATURE", 66, 2);
        snprintf(
            line, sizeof(line), "%.2f C", (double)sample->temperature_c);
        draw_centered_text(line, 94, 4);

        draw_centered_text("HUMIDITY", 158, 2);
        snprintf(
            line, sizeof(line), "%.2f %%",
            (double)sample->humidity_percent);
        draw_centered_text(line, 186, 4);
    } else {
        draw_centered_text("MEASURE FAILED", 100, 3);
        snprintf(
            line, sizeof(line), "ERROR: %s",
            measurement_error_name(measurement_error));
        draw_centered_text(line, 158, 2);
    }

    draw_centered_text("REFRESH: 5 SECONDS", 270, 1);
    return board_display_flush();
}

static void log_display_error(esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "display update failed: %s", esp_err_to_name(error));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "[06] SHTC3 environment sensor");

    const board_display_config_t display_config =
        BOARD_DISPLAY_CONFIG_DEFAULT();
    esp_err_t err = board_display_init(&display_config);
    s_display_ready = err == ESP_OK;
    ESP_LOGI(TAG, "PANEL_INIT: %s", s_display_ready ? "PASS" : "FAIL");
    if (!s_display_ready) {
        ESP_LOGE(TAG, "panel error: %s", esp_err_to_name(err));
    }

    err = board_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
        log_display_error(draw_status_screen("I2C INIT FAILED"));
        return;
    }

    err = shtc3_init(board_i2c_get_handle());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SHTC3 attach failed: %s", esp_err_to_name(err));
        log_display_error(draw_status_screen("SHTC3 ATTACH FAILED"));
        return;
    }

    err = shtc3_read_id(&s_chip_id);
    if (err == ESP_OK) {
        s_chip_id_valid = true;
        /* The ID was read and passed CRC verification; the product-code bits have not been checked. */
        ESP_LOGI(TAG, "chip ID: 0x%04X", (unsigned)s_chip_id);
    } else {
        ESP_LOGW(TAG, "read ID failed: %s", esp_err_to_name(err));
    }
    log_display_error(draw_status_screen("WAITING FOR SAMPLE"));

    TickType_t last_wake_time = xTaskGetTickCount();
    for (;;) {
        shtc3_sample_t sample = {0};
        err = shtc3_measure(&sample);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "T = %.2f C, RH = %.2f %%",
                     (double)sample.temperature_c,
                     (double)sample.humidity_percent);
            log_display_error(draw_environment_screen(&sample, ESP_OK));
        } else {
            ESP_LOGE(TAG, "measure failed (%s)",
                     measurement_error_name(err));
            log_display_error(draw_environment_screen(NULL, err));
        }

        /* Wait relative to the previous scheduled time to reduce drift caused by measurement/drawing time. */
        vTaskDelayUntil(
            &last_wake_time, pdMS_TO_TICKS(MEASUREMENT_INTERVAL_MS));
    }
}
