#include "rlcd_lvgl_port.h"

#include <stddef.h>

#include "board_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdkconfig.h"

#if LV_COLOR_DEPTH != 1
#error "RLCD needs LV_COLOR_DEPTH=1; use sdkconfig.lvgl.defaults"
#endif

/* LVGL I1 uses 1=white and 0=black, opposite to the panel buffer; this layer converts both layout and polarity. */
#define I1_BIT_IS_WHITE true
#define I1_PALETTE_BYTES 8
#define LVGL_TASK_PRIORITY 4

#if CONFIG_FREERTOS_UNICORE
#define LVGL_TASK_CORE 0
#else
#define LVGL_TASK_CORE 1
#endif

static const char *TAG = "rlcd_lvgl";
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void flush_display(
    lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    /* FULL mode renders the whole frame: skip the 8-byte palette, then read 8 horizontal pixels per byte, MSB leftmost.
      * ST7305 uses a different byte layout; these pixels cannot be memcpy'd directly into the panel buffer. */
    pixels += I1_PALETTE_BYTES;
    const int32_t width = area->x2 - area->x1 + 1;
    const int32_t stride = (width + 7) / 8;
    for (int32_t y = area->y1; y <= area->y2; ++y) {
        const uint8_t *row = pixels + (y - area->y1) * stride;
        for (int32_t x = area->x1; x <= area->x2; ++x) {
            const int32_t offset = x - area->x1;
            const bool bit = (row[offset / 8] & (0x80U >> (offset % 8))) != 0;
            const bool white = I1_BIT_IS_WHITE ? bit : !bit;
            board_display_set_pixel(
                (int16_t)x, (int16_t)y,
                white ? BOARD_DISPLAY_WHITE : BOARD_DISPLAY_BLACK);
        }
    }
    /* IDF board_display_flush waits for SPI completion before returning success, unlike Arduino's submit-and-return behavior. */
    const esp_err_t err = board_display_flush();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display flush failed: %s", esp_err_to_name(err));
    }
    /* Return the LVGL source buffer on both success and failure; errors are logged, and LVGL must not wait forever for refresh completion. */
    lv_display_flush_ready(display);
}

bool rlcd_lvgl_port_lock(uint32_t timeout_ms)
{
    if (s_mutex == NULL) {
        return false;
    }
    const TickType_t ticks = timeout_ms == UINT32_MAX
                                 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_mutex, ticks) == pdTRUE;
}

void rlcd_lvgl_port_unlock(void)
{
    xSemaphoreGive(s_mutex);
}

static void lvgl_task(void *argument)
{
    (void)argument;
    ESP_LOGI(TAG, "LVGL task on CPU%d, priority %u",
             (int)xPortGetCoreID(), (unsigned)LVGL_TASK_PRIORITY);
    for (;;) {
        rlcd_lvgl_port_lock(UINT32_MAX);
        /* Widget timers, layout, rendering, and flush all run under this lock; the application must not modify objects concurrently. */
        uint32_t delay_ms = lv_timer_handler();
        rlcd_lvgl_port_unlock();
        if (delay_ms < 10) {
            delay_ms = 10;
        } else if (delay_ms > 100) {
            delay_ms = 100;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t rlcd_lvgl_port_start(void)
{
    if (!board_display_is_ready() || s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const int32_t width = board_display_width();
    const int32_t height = board_display_height();
    /* 400x300: 15000 pixel bytes + an 8-byte palette; this and the board's packed buffer serve different roles, not double buffering. */
    const size_t bytes = (size_t)((width + 7) / 8) * height + I1_PALETTE_BYTES;
    uint8_t *buffer = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *display = lv_display_create(width, height);
    if (display == NULL) {
        heap_caps_free(buffer);
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);
    lv_display_set_buffers(display, buffer, NULL, bytes, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush_display);
    lv_display_set_theme(
        display, lv_theme_mono_init(display, false, &lv_font_montserrat_14));

    if (xTaskCreatePinnedToCore(
            lvgl_task, "rlcd_lvgl", 6144, NULL,
            LVGL_TASK_PRIORITY, &s_task, LVGL_TASK_CORE) != pdPASS) {
        lv_display_delete(display);
        heap_caps_free(buffer);
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "%ldx%ld I1, %u-byte PSRAM buffer",
             (long)width, (long)height, (unsigned)bytes);
    return ESP_OK;
}
