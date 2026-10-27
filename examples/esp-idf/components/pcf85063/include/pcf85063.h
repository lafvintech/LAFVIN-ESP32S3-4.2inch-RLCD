#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;    /* 2000..2099; the chip stores only two year digits, and this course fixes the century. */
    uint8_t month;
    uint8_t day;
    uint8_t weekday;  /* 0=Sunday .. 6=Saturday. */
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} pcf85063_datetime_t;

/* Borrow board_i2c's bus and register the device without taking ownership of the bus lifetime.
  * Singleton, initialized once, with no task mutex; the application must serialize multi-task compound operations. */
esp_err_t pcf85063_init(i2c_master_bus_handle_t bus);
/* ESP_OK only means registers were read; the application must also check OS and datetime_is_valid.
  * OS=1 means the oscillator stopped/was interrupted and the time is untrusted; reading does not clear the flag. */
esp_err_t pcf85063_read(
    pcf85063_datetime_t *datetime, bool *oscillator_stopped);
/* Validate date ranges before writing; write seconds bit 7 as 0 to clear OS, then let the application read back to verify.
  * Only writes the RTC; it does not set ESP32 system time or perform time-zone conversion. */
esp_err_t pcf85063_write(const pcf85063_datetime_t *datetime);

bool pcf85063_datetime_is_valid(const pcf85063_datetime_t *datetime);
uint8_t pcf85063_calculate_weekday(
    uint16_t year, uint8_t month, uint8_t day);

#ifdef __cplusplus
}
#endif
