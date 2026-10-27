idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        board_display
        board_sdcard
        button_service
        freertos
        mono_graphics
)
