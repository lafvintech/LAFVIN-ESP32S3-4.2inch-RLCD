idf_component_register(
    SRCS "board_display.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES
        board_bsp
        esp_driver_gpio
        esp_driver_spi
        esp_lcd
        freertos
        heap
)
