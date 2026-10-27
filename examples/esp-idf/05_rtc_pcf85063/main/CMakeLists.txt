idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        board_bsp
        board_display
        console
        freertos
        mono_graphics
        pcf85063
)
