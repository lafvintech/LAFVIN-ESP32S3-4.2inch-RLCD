#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float latitude;
    float longitude;
    uint32_t timeout_ms;
} weather_client_config_t;

typedef struct {
    float temperature_c;
    float apparent_c;
    float humidity_percent;
    float wind_speed_kmh;
    int wind_direction_degrees;
    float pressure_hpa;
    float precipitation_mm;
    int weather_code;
    float maximum_temperature_c;
    float minimum_temperature_c;
    int rain_chance_percent;
    float maximum_uv_index;
    char sunrise[6];
    char sunset[6];
    char fetched_at[6];
    char error[80];
} weather_data_t;

/* Synchronously perform one HTTPS request and parse in the calling task; Wi-Fi, SNTP, retries, and UI are outside this component.
  * Clear weather first; use results only on ESP_OK, otherwise read error.
  * Missing floating-point fields are NaN and some integer fields are -1; only current temperature is required for parsing.
  * Strings are inline copies suitable for by-value queue transfer; the function frees internal HTTP/JSON resources. */
esp_err_t weather_client_fetch(
    const weather_client_config_t *config, weather_data_t *weather);

const char *weather_client_code_text(int code);
const char *weather_client_wind_direction_text(int degrees);

#ifdef __cplusplus
}
#endif
