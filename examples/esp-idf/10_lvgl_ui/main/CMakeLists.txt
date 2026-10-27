idf_component_register(
    SRCS "app_main.c" "ui.c" "assets/img1.c" "assets/img2.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES board_display button_service freertos rlcd_lvgl_port
)
