#pragma once

/* User settings for weather example 08; rebuild and flash after editing.
 * This file is tracked with the source; clear real Wi-Fi credentials before committing or sharing. */

/* Use 2.4 GHz Wi-Fi; an empty SSID prompts the user to configure it on the page. */
#define APP_WIFI_SSID       ""
#define APP_WIFI_PASSWORD   ""

/* The city label is display-only; use English or Pinyin. Latitude/longitude determine the weather location.
 * Enter coordinates as numbers without quotes; update coordinates and time zone when changing cities. */
#define APP_WEATHER_CITY        "Shanghai"
#define APP_WEATHER_LATITUDE    31.2304f
#define APP_WEATHER_LONGITUDE   121.4737f

/* POSIX TZ format: China Standard Time is CST-8 (UTC+8). */
#define APP_TIME_ZONE    "CST-8"
#define APP_NTP_SERVER   "pool.ntp.org"

/* Main-loop scheduling intervals (ms), not background tasks; synchronous network calls affect actual timing. */
#define APP_WEATHER_REFRESH_MS  (15UL * 60UL * 1000UL)
#define APP_WIFI_RETRY_MS       (60UL * 1000UL)
#define APP_INDOOR_SAMPLE_MS    (30UL * 1000UL)
#define APP_RTC_READ_MS         1000UL
#define APP_WIFI_TIMEOUT_MS     10000UL
