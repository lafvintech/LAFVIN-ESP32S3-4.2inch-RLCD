#pragma once

/* User settings for the 08 weather example. Rebuild and flash after making changes. */

/* Use 2.4 GHz Wi-Fi; an empty SSID prompts configuration on the screen.
  * This file is stored with the source; clear real Wi-Fi credentials before committing or sharing. */
#define APP_WIFI_SSID       ""
#define APP_WIFI_PASSWORD   ""

/* The display font supports ASCII only; use English or pinyin for the city name.
  * Enter coordinates as numbers without quotes: latitude -90..90, longitude -180..180. */

 // Add an example city outside China.
#define APP_WEATHER_CITY        "Shanghai"
#define APP_WEATHER_LATITUDE    31.2304f
#define APP_WEATHER_LONGITUDE   121.4737f

/* POSIX TZ format: China Standard Time is CST-8 (UTC+8).
  * When changing cities, update both the coordinates and the corresponding time zone. */
#define APP_TIME_ZONE    "CST-8"
#define APP_NTP_SERVER   "pool.ntp.org"
