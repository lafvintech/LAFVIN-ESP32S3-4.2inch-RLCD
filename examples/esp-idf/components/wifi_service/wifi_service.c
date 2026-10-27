#include "wifi_service.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define STA_STARTED_BIT      BIT0
#define STA_GOT_IP_BIT       BIT1
#define STA_DISCONNECTED_BIT BIT2
#define STA_RETRY_NOW_BIT    BIT3

static const char *TAG = "wifi_service";

/* The default event loop sets status bits and the STA task waits on them; app_main does not poll. */
static EventGroupHandle_t s_sta_events;
static TaskHandle_t s_sta_task;
static uint32_t s_connect_timeout_ms;
static uint32_t s_retry_interval_ms;
static bool s_started;

static void log_ap_station_count(void)
{
    wifi_sta_list_t stations = {0};
    if (esp_wifi_ap_get_sta_list(&stations) == ESP_OK) {
        ESP_LOGI(TAG, "connected devices: %u", stations.num);
    }
}

static void log_sta_link(void)
{
    wifi_ap_record_t access_point = {0};
    uint8_t mac[6] = {0};

    if (esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) {
        ESP_LOGI(TAG, "online, RSSI=%d dBm", access_point.rssi);
    }
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        ESP_LOGI(TAG, "MAC: " MACSTR, MAC2STR(mac));
    }
}

/* Runs in the default event-loop task, not an ISR.
  * Only record events and signal here; connection timeouts and retry waits belong in sta_connection_task. */
static void wifi_event_handler(
    void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            xEventGroupSetBits(s_sta_events, STA_STARTED_BIT);
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            const wifi_event_sta_disconnected_t *event = event_data;
            ESP_LOGW(TAG, "STA disconnected, reason=%u", event->reason);
            xEventGroupClearBits(s_sta_events, STA_GOT_IP_BIT);
            xEventGroupSetBits(s_sta_events, STA_DISCONNECTED_BIT);
            break;
        }

        case WIFI_EVENT_AP_STACONNECTED: {
            const wifi_event_ap_staconnected_t *event = event_data;
            ESP_LOGI(TAG, "station joined: " MACSTR ", AID=%u",
                     MAC2STR(event->mac), event->aid);
            log_ap_station_count();
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            const wifi_event_ap_stadisconnected_t *event = event_data;
            ESP_LOGI(TAG, "station left: " MACSTR ", AID=%u",
                     MAC2STR(event->mac), event->aid);
            log_ap_station_count();
            break;
        }

        default:
            break;
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        /* Network requests need an IP address, so this event indicates STA readiness. */
        const ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "connected, IP=" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupClearBits(s_sta_events, STA_DISCONNECTED_BIT);
        xEventGroupSetBits(s_sta_events, STA_GOT_IP_BIT);
    }
}

/* STA flow: wait for START -> connect -> wait for IP/disconnect/timeout -> monitor online state -> retry after a delay. */
static void sta_connection_task(void *arg)
{
    (void)arg;

    xEventGroupWaitBits(
        s_sta_events, STA_STARTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    for (;;) {
        xEventGroupClearBits(s_sta_events, STA_DISCONNECTED_BIT);
        ESP_LOGI(TAG, "starting STA connection attempt");

        esp_err_t err = esp_wifi_connect();
        EventBits_t bits = 0;
        if (err == ESP_OK) {
            bits = xEventGroupWaitBits(
                s_sta_events,
                STA_GOT_IP_BIT | STA_DISCONNECTED_BIT,
                pdFALSE,
                pdFALSE,
                pdMS_TO_TICKS(s_connect_timeout_ms));
        } else {
            ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        }

        if ((bits & STA_GOT_IP_BIT) != 0 &&
            (bits & STA_DISCONNECTED_BIT) == 0) {
            log_sta_link();
            for (;;) {
                bits = xEventGroupWaitBits(
                    s_sta_events,
                    STA_DISCONNECTED_BIT,
                    pdTRUE,
                    pdTRUE,
                    pdMS_TO_TICKS(5000));
                if ((bits & STA_DISCONNECTED_BIT) != 0) {
                    break;
                }
                log_sta_link();
            }
        } else if (err == ESP_OK && bits == 0) {
            ESP_LOGW(TAG, "connection timeout");
            esp_wifi_disconnect();
        }

        ESP_LOGI(TAG, "retrying in %" PRIu32 " ms", s_retry_interval_ms);
        xEventGroupWaitBits(
            s_sta_events, STA_RETRY_NOW_BIT,
            pdTRUE, pdTRUE, pdMS_TO_TICKS(s_retry_interval_ms));
    }
}

static esp_err_t start_sta(const wifi_service_config_t *config)
{
    if (config->sta_ssid == NULL || config->sta_ssid[0] == '\0') {
        ESP_LOGE(TAG, "STA SSID is empty; set sta_ssid in the application configuration");
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_create_default_wifi_sta();
    s_sta_events = xEventGroupCreate();
    if (s_sta_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid,
             sizeof(wifi_config.sta.ssid), "%s", config->sta_ssid);
    if (config->sta_password != NULL) {
        snprintf((char *)wifi_config.sta.password,
                 sizeof(wifi_config.sta.password), "%s", config->sta_password);
    }
    /* This example's STA requires WPA2 or stronger security and does not connect to open hotspots. */
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    s_connect_timeout_ms = config->sta_connect_timeout_ms;
    s_retry_interval_ms = config->sta_retry_interval_ms;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(sta_connection_task, "wifi_sta", 4096, NULL, 5,
                    &s_sta_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "mode: STA, SSID=%s", config->sta_ssid);
    return esp_wifi_start();
}

static esp_err_t start_ap(const wifi_service_config_t *config)
{
    if (config->ap_ssid == NULL || config->ap_ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t password_length =
        config->ap_password == NULL ? 0 : strlen(config->ap_password);
    if (password_length > 0 && password_length < 8) {
        ESP_LOGE(TAG, "AP password must be empty or at least 8 characters");
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.ap.ssid,
             sizeof(wifi_config.ap.ssid), "%s", config->ap_ssid);
    wifi_config.ap.ssid_len = strlen(config->ap_ssid);
    if (config->ap_password != NULL) {
        snprintf((char *)wifi_config.ap.password,
                 sizeof(wifi_config.ap.password), "%s", config->ap_password);
    }
    wifi_config.ap.channel = config->ap_channel;
    wifi_config.ap.max_connection = config->ap_max_connections;
    wifi_config.ap.authmode = password_length == 0
                                  ? WIFI_AUTH_OPEN
                                  : WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "mode: AP, SSID=%s, channel=%u",
             config->ap_ssid, config->ap_channel);
    ESP_LOGI(TAG, "the AP does not provide Internet access");
    return esp_wifi_start();
}

esp_err_t wifi_service_start(const wifi_service_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        return err;
    }
    /* Use the application's settings as the source of truth; do not persist this Wi-Fi configuration again in driver NVS. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    if (config->mode == WIFI_SERVICE_MODE_STA) {
        err = start_sta(config);
    } else if (config->mode == WIFI_SERVICE_MODE_AP) {
        err = start_ap(config);
    } else {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK) {
        s_started = true;
    }
    return err;
}

bool wifi_service_sta_is_connected(void)
{
    return s_sta_events != NULL &&
           (xEventGroupGetBits(s_sta_events) & STA_GOT_IP_BIT) != 0;
}

esp_err_t wifi_service_wait_for_sta(uint32_t timeout_ms)
{
    if (s_sta_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        s_sta_events, STA_GOT_IP_BIT, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & STA_GOT_IP_BIT) != 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t wifi_service_request_sta_reconnect(void)
{
    if (s_sta_events == NULL || !s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (wifi_service_sta_is_connected()) {
        esp_err_t err = esp_wifi_disconnect();
        if (err != ESP_OK) {
            return err;
        }
    }
    xEventGroupSetBits(s_sta_events, STA_RETRY_NOW_BIT);
    return ESP_OK;
}
