idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        board_bsp
        board_display
        button_service
        console
        esp_timer
        freertos
        mono_graphics
        pcf85063
        shtc3
)
