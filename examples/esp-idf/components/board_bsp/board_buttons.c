#include "board_buttons.h"

#include <stdint.h>

#include "board_pins.h"
#include "driver/gpio.h"

esp_err_t board_buttons_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BOARD_BOOT_BUTTON_GPIO) |
                        (1ULL << BOARD_USER_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

board_button_state_t board_buttons_read(void)
{
    const board_button_state_t state = {
        .boot_pressed = gpio_get_level(BOARD_BOOT_BUTTON_GPIO) == 0,
        .user_pressed = gpio_get_level(BOARD_USER_BUTTON_GPIO) == 0,
    };
    return state;
}

