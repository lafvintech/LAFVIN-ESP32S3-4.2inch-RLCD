idf_component_register(
    SRCS "button_service.c"
    INCLUDE_DIRS "include"
    REQUIRES board_bsp
    PRIV_REQUIRES esp_timer freertos
)
