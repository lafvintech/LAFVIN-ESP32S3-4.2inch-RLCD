#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* Start once after board_display initialization; this layer owns LVGL, its persistent render buffer, and display refreshes.
  * The task runs on CPU1 in dual-core mode or CPU0 in single-core mode; other tasks must not access board_display directly afterward.
  * Application calls to LVGL require the lock; LVGL timers already hold this non-recursive lock and must not reacquire it.
  * lock is for task context only; UINT32_MAX waits indefinitely. Do not access LVGL if locking fails. */
esp_err_t rlcd_lvgl_port_start(void);
bool rlcd_lvgl_port_lock(uint32_t timeout_ms);
void rlcd_lvgl_port_unlock(void);
