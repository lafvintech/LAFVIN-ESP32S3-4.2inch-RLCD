/* 08 Network weather: app_main owns page state; timers/KEY post events. The network worker
  * waits for Wi-Fi, SNTP, and HTTPS when notified, then posts results by value without touching the display or RTC.
  * Reading order: app_main event branches -> request_weather -> weather_worker_task
  * -> handle_weather_result -> draw_weather_page. User settings are in app_config.h. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "app_config.h"
#include "board_display.h"
#include "board_i2c.h"
#include "button_service.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "mono_graphics.h"
#include "nvs_flash.h"
#include "pcf85063.h"
#include "shtc3.h"
#include "weather_client.h"
#include "wifi_service.h"

#define RTC_REFRESH_MS 1000
#define INDOOR_REFRESH_MS 30000
#define WEATHER_REFRESH_MS (15 * 60 * 1000)
#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_RETRY_INTERVAL_MS 60000
#define NTP_SYNC_TIMEOUT_MS 10000
#define HTTP_TIMEOUT_MS 15000

typedef enum {
    APP_EVENT_SECOND_TICK,
    APP_EVENT_INDOOR_TICK,
    APP_EVENT_WEATHER_DUE,
    APP_EVENT_USER_PRESSED,
    APP_EVENT_WEATHER_RESULT,
} app_event_type_t;

/* Events contain copies of the weather structure and strings, not pointers into the worker's stack. */
typedef struct {
    app_event_type_t type;
    esp_err_t result;
    bool ntp_synced;
    weather_data_t weather;
} app_event_t;

/* Only app_main reads/writes this state; the network task returns results through events instead of sharing it. */
typedef struct {
    bool display_ready;
    bool rtc_present;
    bool rtc_valid;
    pcf85063_datetime_t rtc;
    bool indoor_present;
    bool indoor_valid;
    shtc3_sample_t indoor;
    bool wifi_connected;
    bool weather_valid;
    bool weather_request_active;
    weather_data_t weather;
    char message[80];
    uint8_t displayed_minute;
} dashboard_state_t;

static const char *TAG = "network_weather";
static const char *s_weekdays[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};

static QueueHandle_t s_event_queue;
static TaskHandle_t s_weather_worker;

static void draw_centered(
    int16_t center_x, int16_t y, const char *text, uint8_t scale)
{
    mono_gfx_draw_text(
        center_x - mono_gfx_text_width(text, scale) / 2,
        y, text, BOARD_DISPLAY_BLACK, scale);
}

static void draw_today_cell(
    int index, const char *label, const char *value)
{
    const int16_t center_x = 50 + index * 100;
    draw_centered(center_x, 208, label, 1);
    draw_centered(center_x, 228, value, 2);
}

static void draw_status_page(
    const dashboard_state_t *state, const char *title, const char *detail)
{
    if (!state->display_ready) {
        return;
    }
    board_display_clear(BOARD_DISPLAY_WHITE);
    draw_centered(board_display_width() / 2, 110, title, 2);
    draw_centered(board_display_width() / 2, 150, detail, 1);
    ESP_ERROR_CHECK_WITHOUT_ABORT(board_display_flush());
}

static void draw_weather_page(dashboard_state_t *state)
{
    if (!state->display_ready) {
        return;
    }

    board_display_clear(BOARD_DISPLAY_WHITE);
    char text[80];
    snprintf(text, sizeof(text), "%s [%s]", APP_WEATHER_CITY,
             state->wifi_connected ? "ONLINE" : "OFFLINE");
    mono_gfx_draw_text(12, 10, text, BOARD_DISPLAY_BLACK, 1);

    if (state->rtc_valid) {
        snprintf(text, sizeof(text), "%04u-%02u-%02u %s",
                 (unsigned)state->rtc.year,
                 (unsigned)state->rtc.month,
                 (unsigned)state->rtc.day,
                 s_weekdays[state->rtc.weekday % 7]);
    } else {
        snprintf(text, sizeof(text), "----/--/--");
    }
    mono_gfx_draw_text(
        board_display_width() - 12 - mono_gfx_text_width(text, 1),
        10, text, BOARD_DISPLAY_BLACK, 1);
    mono_gfx_draw_line(
        10, 24, board_display_width() - 10, 24, BOARD_DISPLAY_BLACK);

    mono_gfx_draw_line(205, 32, 205, 112, BOARD_DISPLAY_BLACK);
    if (state->rtc_valid) {
        snprintf(text, sizeof(text), "%02u:%02u",
                 (unsigned)state->rtc.hour, (unsigned)state->rtc.minute);
    } else {
        snprintf(text, sizeof(text), "--:--");
    }
    mono_gfx_draw_text(14, 38, text, BOARD_DISPLAY_BLACK, 4);

    if (state->indoor_valid) {
        snprintf(text, sizeof(text), "IN %.1fC  %.0f%%",
                 (double)state->indoor.temperature_c,
                 (double)state->indoor.humidity_percent);
    } else {
        snprintf(text, sizeof(text), "IN -- (no sensor)");
    }
    mono_gfx_draw_text(14, 84, text, BOARD_DISPLAY_BLACK, 1);

    if (state->weather_valid) {
        snprintf(text, sizeof(text), "%.1fC",
                 (double)state->weather.temperature_c);
        mono_gfx_draw_text(222, 38, text, BOARD_DISPLAY_BLACK, 4);
        mono_gfx_draw_text(
            222, 78,
            weather_client_code_text(state->weather.weather_code),
            BOARD_DISPLAY_BLACK, 1);
        if (isfinite(state->weather.apparent_c)) {
            snprintf(text, sizeof(text), "Feels %.0fC",
                     (double)state->weather.apparent_c);
            mono_gfx_draw_text(222, 96, text, BOARD_DISPLAY_BLACK, 1);
        }
    } else {
        mono_gfx_draw_text(222, 38, "--.-C", BOARD_DISPLAY_BLACK, 4);
        mono_gfx_draw_text(
            222, 84, "no weather yet", BOARD_DISPLAY_BLACK, 1);
    }

    mono_gfx_draw_line(
        10, 120, board_display_width() - 10, 120, BOARD_DISPLAY_BLACK);
    const int16_t column_x[] = {16, 148, 280};
    const char *labels[] = {
        "WIND km/h", "HUMIDITY", "PRESSURE",
        "PRECIP", "HI/LO", "FEELS",
    };
    char values[6][20] = {"--", "--", "--", "--", "--", "--"};
    if (state->weather_valid) {
        if (isfinite(state->weather.wind_speed_kmh)) {
            snprintf(values[0], sizeof(values[0]), "%s %.0f",
                     weather_client_wind_direction_text(
                         state->weather.wind_direction_degrees),
                     (double)state->weather.wind_speed_kmh);
        }
        if (isfinite(state->weather.humidity_percent)) {
            snprintf(values[1], sizeof(values[1]), "%.0f%%",
                     (double)state->weather.humidity_percent);
        }
        if (isfinite(state->weather.pressure_hpa)) {
            snprintf(values[2], sizeof(values[2]), "%.0fhPa",
                     (double)state->weather.pressure_hpa);
        }
        if (isfinite(state->weather.precipitation_mm)) {
            snprintf(values[3], sizeof(values[3]), "%.1fmm",
                     (double)state->weather.precipitation_mm);
        }
        if (isfinite(state->weather.maximum_temperature_c) &&
            isfinite(state->weather.minimum_temperature_c)) {
            snprintf(values[4], sizeof(values[4]), "%.0f/%.0fC",
                     (double)state->weather.maximum_temperature_c,
                     (double)state->weather.minimum_temperature_c);
        }
        if (isfinite(state->weather.apparent_c)) {
            snprintf(values[5], sizeof(values[5]), "%.0fC",
                     (double)state->weather.apparent_c);
        }
    }

    for (int index = 0; index < 6; ++index) {
        const int row = index / 3;
        const int column = index % 3;
        const int16_t y = 128 + row * 34;
        mono_gfx_draw_text(
            column_x[column], y, labels[index], BOARD_DISPLAY_BLACK, 1);
        mono_gfx_draw_text(
            column_x[column], y + 11, values[index], BOARD_DISPLAY_BLACK, 2);
    }

    mono_gfx_draw_line(
        10, 200, board_display_width() - 10, 200, BOARD_DISPLAY_BLACK);
    mono_gfx_draw_line(100, 204, 100, 254, BOARD_DISPLAY_BLACK);
    mono_gfx_draw_line(200, 204, 200, 254, BOARD_DISPLAY_BLACK);
    mono_gfx_draw_line(300, 204, 300, 254, BOARD_DISPLAY_BLACK);

    char today[4][12] = {"--", "--", "--", "--"};
    if (state->weather_valid) {
        if (state->weather.rain_chance_percent >= 0) {
            snprintf(today[0], sizeof(today[0]), "%d%%",
                     state->weather.rain_chance_percent);
        }
        if (isfinite(state->weather.maximum_uv_index)) {
            snprintf(today[1], sizeof(today[1]), "%.1f",
                     (double)state->weather.maximum_uv_index);
        }
        snprintf(today[2], sizeof(today[2]), "%s", state->weather.sunrise);
        snprintf(today[3], sizeof(today[3]), "%s", state->weather.sunset);
    }
    draw_today_cell(0, "RAIN", today[0]);
    draw_today_cell(1, "UV MAX", today[1]);
    draw_today_cell(2, "RISE", today[2]);
    draw_today_cell(3, "SET", today[3]);

    const int16_t status_y = board_display_height() - 34;
    mono_gfx_draw_rect(
        8, status_y, board_display_width() - 16, 24, BOARD_DISPLAY_BLACK);
    snprintf(text, sizeof(text), "%.46s", state->message);
    mono_gfx_draw_text(
        14, status_y + 8, text, BOARD_DISPLAY_BLACK, 1);

    const char *source = "data: open-meteo.com";
    mono_gfx_draw_text(
        board_display_width() - 8 - mono_gfx_text_width(source, 1),
        board_display_height() - 8,
        source, BOARD_DISPLAY_BLACK, 1);
    ESP_ERROR_CHECK_WITHOUT_ABORT(board_display_flush());
    state->displayed_minute = state->rtc_valid ? state->rtc.minute : 0xff;
}

static void read_rtc(dashboard_state_t *state)
{
    if (!state->rtc_present) {
        return;
    }
    pcf85063_datetime_t current = {0};
    bool oscillator_stopped = false;
    const esp_err_t err = pcf85063_read(&current, &oscillator_stopped);
    state->rtc_valid = err == ESP_OK && !oscillator_stopped &&
                       pcf85063_datetime_is_valid(&current);
    if (state->rtc_valid) {
        state->rtc = current;
    }
}

static void read_indoor(dashboard_state_t *state)
{
    if (!state->indoor_present) {
        return;
    }
    state->indoor_valid = shtc3_measure(&state->indoor) == ESP_OK;
}

static bool system_time_to_rtc(pcf85063_datetime_t *datetime)
{
    const time_t now = time(NULL);
    if (now <= 1700000000) {
        return false;
    }
    struct tm local_time = {0};
    if (localtime_r(&now, &local_time) == NULL) {
        return false;
    }
    *datetime = (pcf85063_datetime_t) {
        .year = (uint16_t)(local_time.tm_year + 1900),
        .month = (uint8_t)(local_time.tm_mon + 1),
        .day = (uint8_t)local_time.tm_mday,
        .weekday = (uint8_t)local_time.tm_wday,
        .hour = (uint8_t)local_time.tm_hour,
        .minute = (uint8_t)local_time.tm_min,
        .second = (uint8_t)local_time.tm_sec,
    };
    return pcf85063_datetime_is_valid(datetime);
}

static void configure_timezone(void)
{
    setenv("TZ", APP_TIME_ZONE, 1);
    tzset();
}

static bool set_system_time_from_rtc(const pcf85063_datetime_t *datetime)
{
    /* The RTC stores local date/time; set TZ before mktime converts it to the system's epoch time.
      * Seed the system clock from a trusted RTC at startup; a plausible year does not prove SNTP synchronization. */
    if (!pcf85063_datetime_is_valid(datetime)) {
        return false;
    }

    struct tm local_time = {
        .tm_year = datetime->year - 1900,
        .tm_mon = datetime->month - 1,
        .tm_mday = datetime->day,
        .tm_hour = datetime->hour,
        .tm_min = datetime->minute,
        .tm_sec = datetime->second,
        .tm_isdst = -1,
    };
    const time_t epoch = mktime(&local_time);
    if (epoch <= 0) {
        return false;
    }

    const struct timeval value = {.tv_sec = epoch};
    return settimeofday(&value, NULL) == 0;
}

static bool ensure_time_synchronized(void)
{
    /* Start SNTP only once; after confirmed synchronization, subsequent weather requests no longer block waiting for it. */
    static bool sntp_started;
    static bool sntp_confirmed;
    configure_timezone();

    if (!sntp_started) {
        const esp_sntp_config_t config =
            ESP_NETIF_SNTP_DEFAULT_CONFIG(APP_NTP_SERVER);
        const esp_err_t err = esp_netif_sntp_init(&config);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(err));
            return false;
        }
        sntp_started = true;
    }

    if (sntp_confirmed) {
        return true;
    }
    const esp_err_t err = esp_netif_sntp_sync_wait(
        pdMS_TO_TICKS(NTP_SYNC_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync timeout: %s", esp_err_to_name(err));
        return false;
    }
    sntp_confirmed = time(NULL) > 1700000000;
    return sntp_confirmed;
}

static void weather_worker_task(void *argument)
{
    /* Only this task waits on network operations; the application task can still process local sampling and page events.
      * Do not access the RTC here; copying system time to the RTC belongs in the result handler. */
    (void)argument;
    const weather_client_config_t request_config = {
        .latitude = APP_WEATHER_LATITUDE,
        .longitude = APP_WEATHER_LONGITUDE,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        app_event_t event = {.type = APP_EVENT_WEATHER_RESULT};

        event.result = wifi_service_wait_for_sta(1000);
        if (event.result == ESP_OK) {
            event.ntp_synced = ensure_time_synchronized();
            event.result = weather_client_fetch(
                &request_config, &event.weather);
        } else {
            snprintf(event.weather.error, sizeof(event.weather.error),
                     "Wi-Fi is offline");
        }

        /* Do not drop completion results or active will stay set; the worker may wait for queue space. */
        if (xQueueSend(s_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "failed to deliver weather result");
        }
    }
}

static void request_weather(dashboard_state_t *state)
{
    if (state->weather_request_active) {
        /* Allow at most one in-flight request so repeated button presses cannot build up HTTP requests. */
        return;
    }
    if (!state->wifi_connected) {
        snprintf(state->message, sizeof(state->message),
                 "Wi-Fi offline; press KEY to retry");
        return;
    }

    state->weather_request_active = true;
    snprintf(state->message, sizeof(state->message), "loading weather...");
    xTaskNotifyGive(s_weather_worker);
}

static void handle_weather_result(
    dashboard_state_t *state, const app_event_t *event)
{
    state->weather_request_active = false;
    if (event->result == ESP_OK) {
        state->weather = event->weather;
        state->weather_valid = true;
        if (event->ntp_synced) {
            snprintf(state->message, sizeof(state->message),
                     "weather updated %s", event->weather.fetched_at);
        } else {
            snprintf(state->message, sizeof(state->message),
                     "weather updated; NTP timeout");
        }
        ESP_LOGI(TAG, "weather: %.1f C",
                 (double)event->weather.temperature_c);
    } else {
        snprintf(state->message, sizeof(state->message),
                 "weather error: %.55s", event->weather.error);
        /* On failure, update only the message and keep the last successful weather; weather_valid does not imply fresh data. */
        ESP_LOGW(TAG, "weather request failed: %s",
                 event->weather.error);
    }

    if (event->ntp_synced && state->rtc_present) {
        /* Time synchronization and weather are separate results: confirmed system time may update the RTC even if weather fails. */
        pcf85063_datetime_t system_time = {0};
        if (system_time_to_rtc(&system_time)) {
            const esp_err_t err = pcf85063_write(&system_time);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "RTC update failed: %s", esp_err_to_name(err));
            }
            read_rtc(state);
        }
    }
}

static void post_event(app_event_type_t type)
{
    /* Periodic/button events never wait for queue space; callbacks do not perform HTTP, I2C, or display updates. */
    const app_event_t event = {.type = type};
    if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropped event %d", type);
    }
}

static void timer_callback(TimerHandle_t timer)
{
    post_event((app_event_type_t)(uintptr_t)pvTimerGetTimerID(timer));
}

static void button_callback(button_service_event_t event, void *context)
{
    (void)context;
    if (event == BUTTON_SERVICE_EVENT_USER_PRESSED) {
        post_event(APP_EVENT_USER_PRESSED);
    }
}

static void create_timer(
    const char *name, uint32_t period_ms, app_event_type_t event_type)
{
    TimerHandle_t timer = xTimerCreate(
        name, pdMS_TO_TICKS(period_ms), pdTRUE,
        (void *)(uintptr_t)event_type, timer_callback);
    ESP_ERROR_CHECK(timer == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    ESP_ERROR_CHECK(xTimerStart(timer, 0) == pdPASS
                        ? ESP_OK : ESP_ERR_NO_MEM);
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* This recovery strategy erases the default NVS partition; design a migration strategy before storing user data there. */
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[08] network weather");
    dashboard_state_t state = {.displayed_minute = 0xff};
    snprintf(state.message, sizeof(state.message), "starting...");

    const board_display_config_t display_config =
        BOARD_DISPLAY_CONFIG_DEFAULT();
    esp_err_t err = board_display_init(&display_config);
    state.display_ready = err == ESP_OK;
    if (!state.display_ready) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(err));
    }
    draw_status_page(&state, "starting...", "08 weather station");

    err = board_i2c_init();
    if (err == ESP_OK) {
        state.rtc_present =
            pcf85063_init(board_i2c_get_handle()) == ESP_OK;
        state.indoor_present =
            shtc3_init(board_i2c_get_handle()) == ESP_OK;
    } else {
        ESP_LOGW(TAG, "I2C init failed: %s", esp_err_to_name(err));
    }
    read_rtc(&state);
    read_indoor(&state);
    configure_timezone();
    if (state.rtc_valid && set_system_time_from_rtc(&state.rtc)) {
        ESP_LOGI(TAG, "system clock initialized from RTC");
    }

    s_event_queue = xQueueCreate(16, sizeof(app_event_t));
    ESP_ERROR_CHECK(s_event_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    if (xTaskCreate(
            weather_worker_task, "weather_http", 8192, NULL, 5,
            &s_weather_worker) != pdPASS) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    const button_service_config_t button_config =
        BUTTON_SERVICE_CONFIG_DEFAULT(button_callback, NULL);
    ESP_ERROR_CHECK(button_service_start(&button_config));
    create_timer("weather_second", RTC_REFRESH_MS, APP_EVENT_SECOND_TICK);
    create_timer("weather_indoor", INDOOR_REFRESH_MS, APP_EVENT_INDOOR_TICK);
    create_timer("weather_due", WEATHER_REFRESH_MS, APP_EVENT_WEATHER_DUE);

    init_nvs();
    const wifi_service_config_t wifi_config = {
        .mode = WIFI_SERVICE_MODE_STA,
        .sta_ssid = APP_WIFI_SSID,
        .sta_password = APP_WIFI_PASSWORD,
        .sta_connect_timeout_ms = WIFI_CONNECT_TIMEOUT_MS,
        .sta_retry_interval_ms = WIFI_RETRY_INTERVAL_MS,
    };
    err = wifi_service_start(&wifi_config);
    /* In the event loop below, err retains this startup result, not live connectivity;
      * query wifi_service_sta_is_connected() for online/offline status. */
    if (err != ESP_OK) {
        snprintf(state.message, sizeof(state.message),
                 APP_WIFI_SSID[0] == '\0'
                     ? "set Wi-Fi in app_config.h"
                     : "Wi-Fi start failed");
        ESP_LOGW(TAG, "Wi-Fi start failed: %s", esp_err_to_name(err));
    } else {
        snprintf(state.message, sizeof(state.message), "connecting to Wi-Fi...");
    }
    draw_weather_page(&state);

    for (;;) {
        app_event_t event;
        if (xQueueReceive(s_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        bool redraw = false;
        switch (event.type) {
        case APP_EVENT_SECOND_TICK: {
            read_rtc(&state);
            const bool connected = wifi_service_sta_is_connected();
            if (connected != state.wifi_connected) {
                state.wifi_connected = connected;
                snprintf(state.message, sizeof(state.message),
                         connected ? "Wi-Fi connected" : "Wi-Fi disconnected");
                redraw = true;
                if (connected) {
                    request_weather(&state);
                }
            }
            if (state.rtc_valid &&
                state.rtc.minute != state.displayed_minute) {
                /* The page omits seconds, avoiding full-frame redraws triggered only by the once-per-second RTC read. */
                redraw = true;
            }
            break;
        }
        case APP_EVENT_INDOOR_TICK:
            read_indoor(&state);
            redraw = true;
            break;
        case APP_EVENT_WEATHER_DUE:
            request_weather(&state);
            redraw = true;
            break;
        case APP_EVENT_USER_PRESSED:
            if (state.wifi_connected) {
                request_weather(&state);
            } else if (err == ESP_OK) {
                wifi_service_request_sta_reconnect();
                snprintf(state.message, sizeof(state.message),
                         "requesting Wi-Fi reconnect...");
            } else {
                snprintf(state.message, sizeof(state.message),
                         APP_WIFI_SSID[0] == '\0'
                             ? "set Wi-Fi in app_config.h"
                             : "Wi-Fi start failed");
            }
            redraw = true;
            break;
        case APP_EVENT_WEATHER_RESULT:
            handle_weather_result(&state, &event);
            redraw = true;
            break;
        default:
            break;
        }

        if (redraw) {
            draw_weather_page(&state);
        }
    }
}
