idf_component_register(
    SRCS "app_main.c" "audio_ui.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        board_audio board_display button_service esp_timer freertos
        rlcd_lvgl_port wav_store
)
