idf_component_register(
    SRCS "wav_store.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES board_sdcard
)
