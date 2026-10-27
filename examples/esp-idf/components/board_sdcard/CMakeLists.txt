idf_component_register(
    SRCS "board_sdcard.c"
    INCLUDE_DIRS "include"
    REQUIRES board_bsp fatfs
    PRIV_REQUIRES esp_driver_sdmmc
)
