#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/* One shared I2C bus serves the board; the application initializes it once and device components borrow the handle.
  * Do not call Wire.begin() on the same controller or create a second master bus.
  * Each device sets its SCL rate in add_device; the bus initialization does not set a common rate. */
esp_err_t board_i2c_init(void);
esp_err_t board_i2c_probe(uint8_t address);
i2c_master_bus_handle_t board_i2c_get_handle(void);
/* Remove all device handles and ensure no task is still accessing the bus before releasing it. */
esp_err_t board_i2c_deinit(void);

