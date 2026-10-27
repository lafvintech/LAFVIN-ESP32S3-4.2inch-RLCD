#pragma once

/* User settings for the 02 Wi-Fi example; rebuild and flash after making changes.
 * This file is tracked with the source. Clear real Wi-Fi credentials before committing or sharing. */

/* STA: connect to an existing router. AP: create a local hotspot without Internet forwarding. */
#define APP_WIFI_MODE_STA 1
#define APP_WIFI_MODE_AP  2
#define APP_WIFI_MODE APP_WIFI_MODE_AP

/* STA requires 2.4 GHz Wi-Fi; an empty SSID means it has not been configured. */
#define APP_WIFI_SSID       ""
#define APP_WIFI_PASSWORD   ""
#define APP_WIFI_CONNECT_TIMEOUT_MS 10000U
#define APP_WIFI_RETRY_INTERVAL_MS  15000U

/* Leave the AP password empty for an open hotspot, or use an 8-63-character password.
 * Channels: 1-13; maximum clients: 1-10. Defaults match the original example. */
#define APP_WIFI_AP_SSID            "ESP32S3-RLCD"
#define APP_WIFI_AP_PASSWORD        "12345678"
#define APP_WIFI_AP_CHANNEL         1
#define APP_WIFI_AP_MAX_CONNECTIONS  4

#if APP_WIFI_MODE != APP_WIFI_MODE_STA && APP_WIFI_MODE != APP_WIFI_MODE_AP
#error "APP_WIFI_MODE must be APP_WIFI_MODE_STA or APP_WIFI_MODE_AP"
#endif
