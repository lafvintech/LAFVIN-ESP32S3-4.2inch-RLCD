#include "weather_client.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"

/* Memory limit for the full response, including the trailing NUL; fail on overflow rather than displaying truncated JSON. */
#define WEATHER_RESPONSE_CAPACITY (16 * 1024)

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    bool overflow;
} response_buffer_t;

static void reset_weather(weather_data_t *weather)
{
    memset(weather, 0, sizeof(*weather));
    weather->temperature_c = NAN;
    weather->apparent_c = NAN;
    weather->humidity_percent = NAN;
    weather->wind_speed_kmh = NAN;
    weather->wind_direction_degrees = -1;
    weather->pressure_hpa = NAN;
    weather->precipitation_mm = NAN;
    weather->weather_code = -1;
    weather->maximum_temperature_c = NAN;
    weather->minimum_temperature_c = NAN;
    weather->rain_chance_percent = -1;
    weather->maximum_uv_index = NAN;
    snprintf(weather->sunrise, sizeof(weather->sunrise), "--:--");
    snprintf(weather->sunset, sizeof(weather->sunset), "--:--");
    snprintf(weather->fetched_at, sizeof(weather->fetched_at), "--:--");
}

static void set_error(weather_data_t *weather, const char *message)
{
    snprintf(weather->error, sizeof(weather->error), "%s", message);
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    /* A response may arrive in multiple chunks; ON_DATA does not mean the entire JSON has been received. */
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len == 0) {
        return ESP_OK;
    }

    response_buffer_t *response = event->user_data;
    const size_t incoming = (size_t)event->data_len;
    if (response->length + incoming >= response->capacity) {
        response->overflow = true;
        return ESP_FAIL;
    }

    memcpy(response->data + response->length, event->data, incoming);
    response->length += incoming;
    response->data[response->length] = '\0'; /* Reserve one byte for subsequent string handling. */
    return ESP_OK;
}

static bool get_number(
    const cJSON *object, const char *name, float *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *value = (float)item->valuedouble;
    return true;
}

static bool get_integer(
    const cJSON *object, const char *name, int *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *value = item->valueint;
    return true;
}

static const cJSON *first_array_item(
    const cJSON *object, const char *name)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsArray(array) ? cJSON_GetArrayItem(array, 0) : NULL;
}

static void get_array_number(
    const cJSON *object, const char *name, float *value)
{
    const cJSON *item = first_array_item(object, name);
    if (cJSON_IsNumber(item)) {
        *value = (float)item->valuedouble;
    }
}

static void get_array_integer(
    const cJSON *object, const char *name, int *value)
{
    const cJSON *item = first_array_item(object, name);
    if (cJSON_IsNumber(item)) {
        *value = item->valueint;
    }
}

static void extract_clock_text(const char *iso_time, char output[6])
{
    if (iso_time == NULL) {
        return;
    }
    const char *separator = strchr(iso_time, 'T');
    if (separator != NULL && strlen(separator) >= 6) {
        snprintf(output, 6, "%.5s", separator + 1);
    }
}

static esp_err_t parse_weather(
    const char *json, size_t length, weather_data_t *weather)
{
    cJSON *document = cJSON_ParseWithLength(json, length);
    if (document == NULL) {
        set_error(weather, "invalid JSON response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *current =
        cJSON_GetObjectItemCaseSensitive(document, "current");
    const cJSON *daily =
        cJSON_GetObjectItemCaseSensitive(document, "daily");
    if (!cJSON_IsObject(current) || !cJSON_IsObject(daily)) {
        cJSON_Delete(document);
        set_error(weather, "weather data missing");
        return ESP_ERR_INVALID_RESPONSE;
    }

    get_number(current, "temperature_2m", &weather->temperature_c);
    get_number(current, "apparent_temperature", &weather->apparent_c);
    get_number(current, "relative_humidity_2m", &weather->humidity_percent);
    get_number(current, "wind_speed_10m", &weather->wind_speed_kmh);
    get_integer(
        current, "wind_direction_10m", &weather->wind_direction_degrees);
    get_number(current, "surface_pressure", &weather->pressure_hpa);
    get_number(current, "precipitation", &weather->precipitation_mm);
    get_integer(current, "weather_code", &weather->weather_code);

    get_array_number(
        daily, "temperature_2m_max", &weather->maximum_temperature_c);
    get_array_number(
        daily, "temperature_2m_min", &weather->minimum_temperature_c);
    get_array_integer(
        daily, "precipitation_probability_max",
        &weather->rain_chance_percent);
    get_array_number(daily, "uv_index_max", &weather->maximum_uv_index);

    /* Use only day 0 of the daily array; copy strings into the result structure before deleting the JSON document. */
    const cJSON *sunrise = first_array_item(daily, "sunrise");
    const cJSON *sunset = first_array_item(daily, "sunset");
    if (cJSON_IsString(sunrise)) {
        extract_clock_text(sunrise->valuestring, weather->sunrise);
    }
    if (cJSON_IsString(sunset)) {
        extract_clock_text(sunset->valuestring, weather->sunset);
    }

    cJSON_Delete(document);
    if (!isfinite(weather->temperature_c)) {
        set_error(weather, "temperature missing");
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Record local request-success time, not the API's weather observation time; the application must set the time zone first. */
    const time_t now = time(NULL);
    struct tm local_time = {0};
    if (now > 1700000000 && localtime_r(&now, &local_time) != NULL) {
        strftime(
            weather->fetched_at, sizeof(weather->fetched_at),
            "%H:%M", &local_time);
    }
    weather->error[0] = '\0';
    return ESP_OK;
}

esp_err_t weather_client_fetch(
    const weather_client_config_t *config, weather_data_t *weather)
{
    if (weather != NULL) {
        reset_weather(weather);
    }
    if (config == NULL || weather == NULL ||
        config->latitude < -90.0f || config->latitude > 90.0f ||
        config->longitude < -180.0f || config->longitude > 180.0f ||
        config->timeout_ms == 0) {
        if (weather != NULL) {
            set_error(weather, "invalid weather configuration");
        }
        return ESP_ERR_INVALID_ARG;
    }

    char url[768];
    const int url_length = snprintf(
        url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?"
        "latitude=%.4f&longitude=%.4f&"
        "current=temperature_2m,relative_humidity_2m,"
        "apparent_temperature,weather_code,wind_speed_10m,"
        "wind_direction_10m,surface_pressure,precipitation&"
        "daily=temperature_2m_max,temperature_2m_min,"
        "precipitation_probability_max,uv_index_max,sunrise,sunset&"
        "timezone=auto&forecast_days=1",
        (double)config->latitude, (double)config->longitude);
    if (url_length < 0 || (size_t)url_length >= sizeof(url)) {
        set_error(weather, "request URL is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    char *payload = malloc(WEATHER_RESPONSE_CAPACITY);
    if (payload == NULL) {
        set_error(weather, "response allocation failed");
        return ESP_ERR_NO_MEM;
    }
    response_buffer_t response = {
        .data = payload,
        .capacity = WEATHER_RESPONSE_CAPACITY,
    };
    payload[0] = '\0';

    const esp_http_client_config_t http_config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = config->timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach, /* Verify the HTTPS server using IDF's certificate bundle. */
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        free(payload);
        set_error(weather, "HTTP client init failed");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (response.overflow) {
        err = ESP_ERR_INVALID_SIZE;
        set_error(weather, "weather response too large");
    } else if (err != ESP_OK) {
        snprintf(
            weather->error, sizeof(weather->error),
            "request failed: %s", esp_err_to_name(err));
    } else if (status != 200) {
        err = ESP_FAIL;
        snprintf(weather->error, sizeof(weather->error), "HTTP %d", status);
    } else {
        err = parse_weather(response.data, response.length, weather);
    }

    /* All result fields have been copied; weather remains usable after freeing the raw response. */
    free(payload);
    return err;
}

const char *weather_client_code_text(int code)
{
    if (code == 0) return "Clear";
    if (code >= 1 && code <= 2) return "Part cloudy";
    if (code == 3) return "Overcast";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if ((code >= 61 && code <= 67) ||
        (code >= 80 && code <= 82)) return "Rain";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "Snow";
    if (code >= 95) return "Thunderstorm";
    return "Unknown";
}

const char *weather_client_wind_direction_text(int degrees)
{
    static const char *directions[] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW",
    };
    if (degrees < 0) {
        return "--";
    }
    return directions[((degrees + 22) / 45) % 8];
}
