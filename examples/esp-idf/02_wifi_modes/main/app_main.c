/* 02 Wi-Fi: this file initializes NVS and configures the mode; wifi_service handles connection and retries.
  * Edit app_config.h in this directory; no menuconfig changes are required. */
#include "app_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_service.h"

static const char *TAG = "wifi_demo";

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* This example's recovery strategy erases the entire default NVS partition, not just Wi-Fi settings.
          * If user data is added later, design a migration/recovery strategy instead of reusing this erase step. */
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    init_nvs();

#if APP_WIFI_MODE == APP_WIFI_MODE_AP
    const wifi_service_config_t config = {
        .mode = WIFI_SERVICE_MODE_AP,
        .ap_ssid = APP_WIFI_AP_SSID,
        .ap_password = APP_WIFI_AP_PASSWORD,
        .ap_channel = APP_WIFI_AP_CHANNEL,
        .ap_max_connections = APP_WIFI_AP_MAX_CONNECTIONS,
    };
#else
    const wifi_service_config_t config = {
        .mode = WIFI_SERVICE_MODE_STA,
        .sta_ssid = APP_WIFI_SSID,
        .sta_password = APP_WIFI_PASSWORD,
        .sta_connect_timeout_ms = APP_WIFI_CONNECT_TIMEOUT_MS,
        .sta_retry_interval_ms = APP_WIFI_RETRY_INTERVAL_MS,
    };
#endif

    ESP_LOGI(TAG, "[02] Wi-Fi modes");
    ESP_ERROR_CHECK(wifi_service_start(&config));
    /* Success only means startup succeeded; it does not guarantee that STA has obtained an IP address.
      * app_main may return while the event loop and STA connection task keep running. */
}

