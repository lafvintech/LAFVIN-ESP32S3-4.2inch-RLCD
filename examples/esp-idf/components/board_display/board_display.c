#include "board_display.h"

#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define ST7305_MAX_SPI_CLOCK_HZ (24U * 1000U * 1000U)
#define ST7305_RAM_COLUMN_ORIGIN 0x12
#define DISPLAY_FLUSH_TIMEOUT_MS 1000

typedef struct {
    uint8_t command;
    uint8_t data[10];
    uint8_t data_size;
    uint16_t delay_ms;
} st7305_init_command_t;

typedef struct {
    esp_lcd_panel_io_handle_t io;
    SemaphoreHandle_t flush_done;
    uint8_t *frame_buffer;
    size_t frame_buffer_size;
    int16_t width;
    int16_t height;
    uint8_t column_start;
    uint8_t column_end;
    uint8_t page_start;
    uint8_t page_end;
    bool landscape;
    bool spi_bus_initialized;
    bool ready;
} board_display_context_t;

static board_display_context_t s_display;

/* Use this panel's reference power-on parameters, not a generic ST7305 configuration.
  * Each entry contains: command, parameters, parameter byte count, and post-send delay in milliseconds.
  * Check supply/drive voltage settings and execution order as a whole; do not change them to adjust the UI. */
static const st7305_init_command_t s_init_commands[] = {
    {0xD6, {0x17, 0x02}, 2, 0},
    {0xD1, {0x01}, 1, 0},
    {0xC0, {0x11, 0x04}, 2, 0},
    {0xC1, {0x69, 0x69, 0x69, 0x69}, 4, 0},
    {0xC2, {0x19, 0x19, 0x19, 0x19}, 4, 0},
    {0xC4, {0x4B, 0x4B, 0x4B, 0x4B}, 4, 0},
    {0xC5, {0x19, 0x19, 0x19, 0x19}, 4, 0},
    {0xD8, {0x80, 0xE9}, 2, 0},
    {0xB2, {0x02}, 1, 0},
    {0xB3, {0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}, 10, 0},
    {0xB4, {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}, 8, 0},
    {0x62, {0x32, 0x03, 0x1F}, 3, 0},
    {0xB7, {0x13}, 1, 0},
    {0xB0, {0x64}, 1, 0},
    {0x11, {0}, 0, 200}, /* Wait for internal power to stabilize after exiting sleep. */
    {0xC9, {0x00}, 1, 0},
    {0x36, {0x48}, 1, 0}, /* Scan settings match the landscape/portrait pixel mappings below. */
    {0x3A, {0x11}, 1, 0},
    {0xB9, {0x20}, 1, 0},
    {0xB8, {0x29}, 1, 0},
    {0x20, {0}, 0, 0},
};

static const st7305_init_command_t s_post_window_commands[] = {
    {0x35, {0x00}, 1, 0},
    {0xD0, {0xFF}, 1, 0},
    {0x38, {0}, 0, 0},
    {0x29, {0}, 0, 0},
};

static bool on_color_transfer_done(
    esp_lcd_panel_io_handle_t io,
    esp_lcd_panel_io_event_data_t *event_data,
    void *user_context)
{
    (void)io;
    (void)event_data;

    /* The SPI-completion ISR only wakes the waiter; do not draw, log, or block here. */
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(
        (SemaphoreHandle_t)user_context, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

static esp_err_t send_command(
    uint8_t command, const uint8_t *data, size_t data_size)
{
    return esp_lcd_panel_io_tx_param(
        s_display.io, command, data_size == 0 ? NULL : data, data_size);
}

static void release_partial_init(void)
{
    if (s_display.io != NULL) {
        esp_lcd_panel_io_del(s_display.io);
    }
    if (s_display.spi_bus_initialized) {
        spi_bus_free(SPI3_HOST);
    }
    if (s_display.frame_buffer != NULL) {
        heap_caps_free(s_display.frame_buffer);
    }
    if (s_display.flush_done != NULL) {
        vSemaphoreDelete(s_display.flush_done);
    }
    memset(&s_display, 0, sizeof(s_display));
}

static esp_err_t hard_reset(void)
{
    /* Follow the reference sequence: high 50 ms -> low 20 ms -> high 50 ms; retain the settling delays. */
    esp_err_t err = gpio_set_level(BOARD_LCD_RESET_GPIO, 1);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    err = gpio_set_level(BOARD_LCD_RESET_GPIO, 0);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    err = gpio_set_level(BOARD_LCD_RESET_GPIO, 1);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static esp_err_t apply_power_on_sequence(void)
{
    esp_err_t err = hard_reset();
    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0; i < sizeof(s_init_commands) / sizeof(s_init_commands[0]); ++i) {
        const st7305_init_command_t *entry = &s_init_commands[i];
        err = send_command(entry->command, entry->data, entry->data_size);
        if (err != ESP_OK) {
            return err;
        }
        if (entry->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(entry->delay_ms));
        }
    }

    const uint8_t column_window[] = {
        s_display.column_start, s_display.column_end,
    };
    err = send_command(0x2A, column_window, sizeof(column_window));
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t page_window[] = {
        s_display.page_start, s_display.page_end,
    };
    err = send_command(0x2B, page_window, sizeof(page_window));
    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0;
         i < sizeof(s_post_window_commands) /
                 sizeof(s_post_window_commands[0]);
         ++i) {
        const st7305_init_command_t *entry = &s_post_window_commands[i];
        err = send_command(entry->command, entry->data, entry->data_size);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static void derive_geometry(board_display_orientation_t orientation)
{
    if (orientation == BOARD_DISPLAY_LANDSCAPE) {
        s_display.width = BOARD_LCD_LANDSCAPE_WIDTH;
        s_display.height = BOARD_LCD_LANDSCAPE_HEIGHT;
        s_display.landscape = true;
    } else {
        s_display.width = BOARD_LCD_PORTRAIT_WIDTH;
        s_display.height = BOARD_LCD_PORTRAIT_HEIGHT;
        s_display.landscape = false;
    }

    const int16_t source_lines = s_display.width < s_display.height
                                     ? s_display.width
                                     : s_display.height;
    const int16_t gate_lines = s_display.width > s_display.height
                                   ? s_display.width
                                   : s_display.height;
    /* This panel has 300 source lines / 400 gate lines, mapped to columns 0x12..0x2A and pages 0x00..0xC7.
      * These are controller RAM-window units, not UI pixel coordinates. */
    s_display.column_start = ST7305_RAM_COLUMN_ORIGIN;
    s_display.column_end =
        ST7305_RAM_COLUMN_ORIGIN + source_lines / 12 - 1;
    s_display.page_start = 0;
    s_display.page_end = gate_lines / 2 - 1;
    s_display.frame_buffer_size =
        (size_t)s_display.width * s_display.height / 8;
}

esp_err_t board_display_init(const board_display_config_t *config)
{
    if (config == NULL ||
        (config->orientation != BOARD_DISPLAY_LANDSCAPE &&
         config->orientation != BOARD_DISPLAY_PORTRAIT) ||
        config->spi_clock_hz == 0 ||
        config->spi_clock_hz > ST7305_MAX_SPI_CLOCK_HZ) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_display.ready || s_display.spi_bus_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    derive_geometry(config->orientation);
    /* 400 * 300 / 8 = 15000 bytes; there is no internal-RAM fallback, so onboard PSRAM must be enabled. */
    s_display.frame_buffer = heap_caps_malloc(
        s_display.frame_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_display.frame_buffer == NULL) {
        release_partial_init();
        return ESP_ERR_NO_MEM;
    }

    /* This semaphore signals transfer completion; it is not a framebuffer mutex. */
    s_display.flush_done = xSemaphoreCreateBinary();
    if (s_display.flush_done == NULL) {
        release_partial_init();
        return ESP_ERR_NO_MEM;
    }

    const spi_bus_config_t bus_config = {
        .mosi_io_num = BOARD_LCD_MOSI_GPIO,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = BOARD_LCD_SCLK_GPIO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = (int)s_display.frame_buffer_size,
    };
    esp_err_t err = spi_bus_initialize(
        SPI3_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        release_partial_init();
        return err;
    }
    s_display.spi_bus_initialized = true;

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = BOARD_LCD_CS_GPIO,
        .dc_gpio_num = BOARD_LCD_DC_GPIO,
        .spi_mode = 0,
        .pclk_hz = config->spi_clock_hz,
        .trans_queue_depth = 1,
        .on_color_trans_done = on_color_transfer_done,
        .user_ctx = s_display.flush_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_config, &s_display.io);
    if (err != ESP_OK) {
        release_partial_init();
        return err;
    }

    const gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << BOARD_LCD_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&reset_config);
    if (err != ESP_OK) {
        release_partial_init();
        return err;
    }

    err = apply_power_on_sequence();
    if (err != ESP_OK) {
        release_partial_init();
        return err;
    }

    memset(s_display.frame_buffer, 0, s_display.frame_buffer_size);
    s_display.ready = true;
    return ESP_OK;
}

bool board_display_is_ready(void)
{
    return s_display.ready;
}

int16_t board_display_width(void)
{
    return s_display.width;
}

int16_t board_display_height(void)
{
    return s_display.height;
}

size_t board_display_frame_buffer_size(void)
{
    return s_display.frame_buffer_size;
}

void board_display_clear(board_display_color_t color)
{
    if (!s_display.ready) {
        return;
    }
    memset(s_display.frame_buffer,
           color == BOARD_DISPLAY_BLACK ? 0xFF : 0x00,
           s_display.frame_buffer_size);
}

static void write_portrait_pixel(
    int16_t x, int16_t y, bool black)
{
    /* Each byte covers 4 columns x 2 rows; bit = 7 - ((x%4)*2 + y%2). */
    const int16_t cell_x = x >> 2;
    const int16_t row_pair = y >> 1;
    const size_t byte_index =
        (size_t)row_pair * (s_display.width >> 2) + cell_x;
    const uint8_t lane = ((x & 3) << 1) | (y & 1);
    const uint8_t mask = 1U << (7 - lane);

    if (black) {
        s_display.frame_buffer[byte_index] |= mask;
    } else {
        s_display.frame_buffer[byte_index] &= (uint8_t)~mask;
    }
}

static void write_landscape_pixel(
    int16_t x, int16_t y, bool black)
{
    /* Each byte covers 2 columns x 4 rows; flip y first, then pack along columns.
      * This is not a row-major bitmap; the graphics layer must write logical pixels through set_pixel. */
    const int16_t flipped_y = s_display.height - 1 - y;
    const int16_t column_cell = x >> 1;
    const int16_t row_block = flipped_y >> 2;
    const size_t byte_index =
        (size_t)column_cell * (s_display.height >> 2) + row_block;
    const uint8_t lane = ((flipped_y & 3) << 1) | (x & 1);
    const uint8_t mask = 1U << (7 - lane);

    if (black) {
        s_display.frame_buffer[byte_index] |= mask;
    } else {
        s_display.frame_buffer[byte_index] &= (uint8_t)~mask;
    }
}

void board_display_set_pixel(
    int16_t x, int16_t y, board_display_color_t color)
{
    if (!s_display.ready || x < 0 || y < 0 ||
        x >= s_display.width || y >= s_display.height) {
        return;
    }

    const bool black = color == BOARD_DISPLAY_BLACK;
    if (s_display.landscape) {
        write_landscape_pixel(x, y, black);
    } else {
        write_portrait_pixel(x, y, black);
    }
}

esp_err_t board_display_flush(void)
{
    if (!s_display.ready) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t column_window[] = {
        s_display.column_start, s_display.column_end,
    };
    esp_err_t err = send_command(0x2A, column_window, sizeof(column_window));
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t page_window[] = {
        s_display.page_start, s_display.page_end,
    };
    err = send_command(0x2B, page_window, sizeof(page_window));
    if (err != ESP_OK) {
        return err;
    }

    /* Clear a completion signal that arrived late after the previous timeout; do not mistake it for this frame's completion. */
    (void)xSemaphoreTake(s_display.flush_done, 0);
    err = esp_lcd_panel_io_tx_color(
        s_display.io, 0x2C,
        s_display.frame_buffer, s_display.frame_buffer_size);
    if (err != ESP_OK) {
        return err;
    }

    /* tx_color only submits an asynchronous transfer; wait for completion before returning so the buffer can be reused on success. */
    if (xSemaphoreTake(
            s_display.flush_done,
            pdMS_TO_TICKS(DISPLAY_FLUSH_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
