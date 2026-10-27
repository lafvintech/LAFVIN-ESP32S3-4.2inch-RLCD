#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    bool boot_pressed;
    bool user_pressed;
} board_button_state_t;

esp_err_t board_buttons_init(void);
board_button_state_t board_buttons_read(void);

