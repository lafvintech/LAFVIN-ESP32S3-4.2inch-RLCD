#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature_c;
    float humidity_percent;
} shtc3_sample_t;

/* Borrow the existing shared bus and register a 7-bit-address device; do not probe the chip or create a task.
  * Singleton with no compound-operation mutex; callers must not run measurements and ID reads concurrently. */
esp_err_t shtc3_init(i2c_master_bus_handle_t bus);
/* Read the ID and verify CRC; does not wake/sleep the device or check its product code. */
esp_err_t shtc3_read_id(uint16_t *chip_id);
/* Synchronously wake -> wait for measurement -> read -> sleep -> check CRC/convert; use sample only on ESP_OK.
  * Includes task delays; do not call from an ISR or software-timer callback. Failure leaves the previous sample unchanged. */
esp_err_t shtc3_measure(shtc3_sample_t *sample);

#ifdef __cplusplus
}
#endif
