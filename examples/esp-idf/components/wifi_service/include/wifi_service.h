#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    WIFI_SERVICE_MODE_STA,
    WIFI_SERVICE_MODE_AP,
} wifi_service_mode_t;

typedef struct {
    wifi_service_mode_t mode;

    const char *sta_ssid;
    const char *sta_password;
    uint32_t sta_connect_timeout_ms;
    uint32_t sta_retry_interval_ms;

    const char *ap_ssid;
    const char *ap_password;
    uint8_t ap_channel;
    uint8_t ap_max_connections;
} wifi_service_config_t;

/* The application initializes NVS before starting this service once; stop and dynamic mode-switch APIs are not provided.
  * Configuration is used/copied during startup; ESP_OK means startup succeeded, not that STA is online. */
esp_err_t wifi_service_start(const wifi_service_config_t *config);
/* Readiness requires GOT_IP, not just a wireless connection to the router. */
bool wifi_service_sta_is_connected(void);
/* Block the calling task until an IP address is obtained or timeout; do not wait in a default event-loop callback. */
esp_err_t wifi_service_wait_for_sta(uint32_t timeout_ms);
/* Request a retry from the connection task; do not synchronously wait for the connection here. */
esp_err_t wifi_service_request_sta_reconnect(void);
