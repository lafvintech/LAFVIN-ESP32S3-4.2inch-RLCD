idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        board_display
        board_bsp
        button_service
        esp_netif
        freertos
        mono_graphics
        nvs_flash
        pcf85063
        shtc3
        weather_client
        wifi_service
)
