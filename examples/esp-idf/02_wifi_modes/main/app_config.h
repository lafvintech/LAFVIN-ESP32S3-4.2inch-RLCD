#pragma once

/* User settings for the 02 Wi-Fi example; rebuild and flash after making changes.
  * This file is stored with the source; clear real Wi-Fi credentials before committing or sharing. */

/* STA connects to an existing router; AP creates a local hotspot without Internet forwarding. */
#define APP_WIFI_MODE_STA 1
#define APP_WIFI_MODE_AP  2
#define APP_WIFI_MODE APP_WIFI_MODE_AP

/* STA uses 2.4 GHz Wi-Fi; an empty SSID means it has not been configured. */
#define APP_WIFI_SSID       ""
#define APP_WIFI_PASSWORD   ""
#define APP_WIFI_CONNECT_TIMEOUT_MS 10000U
#define APP_WIFI_RETRY_INTERVAL_MS  15000U

/* An empty AP password creates an open hotspot; otherwise, use 8-63 characters.
  * Valid channels are 1-13 and client limits are 1-10; defaults match the original example. */
#define APP_WIFI_AP_SSID            "ESP32S3-RLCD"
#define APP_WIFI_AP_PASSWORD        "12345678"
#define APP_WIFI_AP_CHANNEL         1
#define APP_WIFI_AP_MAX_CONNECTIONS  4

#if APP_WIFI_MODE != APP_WIFI_MODE_STA && APP_WIFI_MODE != APP_WIFI_MODE_AP
#error "APP_WIFI_MODE must be APP_WIFI_MODE_STA or APP_WIFI_MODE_AP"
#endif
