idf_component_register(
    SRCS "rlcd_lvgl_port.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common lvgl__lvgl
    PRIV_REQUIRES board_display esp_timer freertos heap
)
