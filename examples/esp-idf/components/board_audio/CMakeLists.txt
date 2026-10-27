idf_component_register(
    SRCS "board_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES board_bsp esp_driver_i2s espressif__esp_codec_dev
)
