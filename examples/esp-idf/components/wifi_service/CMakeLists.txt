idf_component_register(
    SRCS "wifi_service.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_event
        esp_netif
        esp_wifi
    PRIV_REQUIRES freertos
)
