idf_component_register(
    SRCS "shtc3.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_common
        esp_driver_i2c
    PRIV_REQUIRES
        esp_rom
        freertos
)
