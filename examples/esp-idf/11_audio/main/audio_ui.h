#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    /* The queue copies the entire snapshot, including inline strings, not pointers to producer stack or mutable buffers.
      * The producer ensures strings are '\0'-terminated, percentages are 0..100, and time values are in ms. */
    char state[16];
    char detail[80];
    char file_name[16];
    uint8_t mic_percent;
    uint8_t playback_percent;
    uint32_t elapsed_ms;
    uint32_t total_ms;
    bool show_total;
} audio_ui_data_t;

/* Call once at startup with the rlcd_lvgl_port lock held to create the UI, single-slot queue, and LVGL consumption timer. */
esp_err_t audio_ui_init(void);

/* For task-context calls after initialization: overwrite with the latest snapshot without waiting for the consumer or taking the LVGL lock.
  * These are states, not events: intermediate UI snapshots may be skipped, and immediate display is not guaranteed. */
esp_err_t audio_ui_publish(const audio_ui_data_t *data);
