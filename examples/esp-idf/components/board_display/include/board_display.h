#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_DISPLAY_DEFAULT_CLOCK_HZ (10U * 1000U * 1000U)

typedef enum {
    BOARD_DISPLAY_WHITE = 0,
    BOARD_DISPLAY_BLACK = 1,
} board_display_color_t;

typedef enum {
    BOARD_DISPLAY_LANDSCAPE,
    BOARD_DISPLAY_PORTRAIT,
} board_display_orientation_t;

typedef struct {
    board_display_orientation_t orientation;
    uint32_t spi_clock_hz;
} board_display_config_t;

#define BOARD_DISPLAY_CONFIG_DEFAULT()                  \
    {                                                   \
        .orientation = BOARD_DISPLAY_LANDSCAPE,         \
        .spi_clock_hz = BOARD_DISPLAY_DEFAULT_CLOCK_HZ, \
    }

/* Board-level singleton: create SPI3, panel IO, and the PSRAM framebuffer, then run the reset/initialization sequence.
  * Only the board's two orientations are supported; success clears the buffer to white, but the application must flush the first frame. */
esp_err_t board_display_init(const board_display_config_t *config);

bool board_display_is_ready(void);
int16_t board_display_width(void);
int16_t board_display_height(void);
size_t board_display_frame_buffer_size(void);

/* Update the framebuffer only: 0=white, 1=black; origin is top-left and out-of-bounds pixels are ignored. */
void board_display_clear(board_display_color_t color);
void board_display_set_pixel(
    int16_t x, int16_t y, board_display_color_t color);
/* Send a full frame; ESP_OK means SPI transmission is complete and the buffer may be modified again.
  * A timeout does not cancel the transfer. This component has no drawing mutex; multitasking callers must protect
  * the entire clear -> draw -> flush sequence and must not modify the same buffer during transmission. */
esp_err_t board_display_flush(void);

#ifdef __cplusplus
}
#endif
