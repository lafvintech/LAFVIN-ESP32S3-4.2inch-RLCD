#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool ok;
    uint16_t raw_average;      /* Average raw ADC value, not a voltage. */
    uint16_t pin_millivolts;   /* Calibrated voltage at the ADC pin, in mV. */
    float battery_voltage;    /* Battery voltage after undoing the board's voltage divider, in V. */
    uint8_t estimated_percent; /* Linear voltage mapping for a rough display only. */
} board_battery_sample_t;

/* This component owns ADC1 and calibration handles; init once and deinit when no longer used. */
esp_err_t board_battery_init(void);
/* Sample synchronously in the calling task, with brief waits between samples; do not call from an ISR.
  * Use sample only on ESP_OK; this component does not protect concurrent read/deinit calls. */
esp_err_t board_battery_read(size_t sample_count, board_battery_sample_t *sample);
esp_err_t board_battery_deinit(void);

