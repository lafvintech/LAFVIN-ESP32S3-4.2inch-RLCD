idf_component_register(
    SRCS
        "board_adc.c"
        "board_buttons.c"
        "board_i2c.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_adc
        esp_driver_gpio
        esp_driver_i2c
)

