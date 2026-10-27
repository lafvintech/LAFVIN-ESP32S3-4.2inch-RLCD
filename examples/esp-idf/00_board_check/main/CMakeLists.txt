idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        board_bsp
        esp_system
        heap
        spi_flash
)

