/* 07 Local dashboard: timers, buttons, and the console only post events; app_main samples and draws sequentially.
  * Pages and application state stay in this file; shared components handle hardware details without extra layers.
  * This version reuses board_display and does not run the Arduino version's lookup-table benchmark. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_adc.h"
#include "board_display.h"
#include "board_i2c.h"
#include "button_service.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "mono_graphics.h"
#include "pcf85063.h"
#include "sdkconfig.h"
#include "shtc3.h"

#define FAST_REFRESH_MS 1000
#define ENVIRONMENT_REFRESH_MS 10000
#define BATTERY_SAMPLE_COUNT 16

typedef enum {
    APP_EVENT_FAST_TICK,
    APP_EVENT_ENVIRONMENT_TICK,
    APP_EVENT_USER_PRESSED,
    APP_EVENT_BOOT_PRESSED,
    APP_EVENT_SET_RTC,
} app_event_type_t;

/* The queue copies entire events by value; do not replace the date with a pointer to a console-local variable. */
typedef struct {
    app_event_type_t type;
    pcf85063_datetime_t datetime;
} app_event_t;

/* Only app_main owns and modifies state; callbacks do not access it directly, so no state mutex is needed.
  * present/ready indicate initialization results; valid indicates the latest read result. Do not confuse them. */
typedef struct {
    bool display_ready;
    bool rtc_present;
    bool rtc_valid;
    bool rtc_oscillator_stopped;
    pcf85063_datetime_t now;

    bool environment_present;
    bool environment_valid;
    shtc3_sample_t environment;

    bool battery_ready;
    bool battery_valid;
    board_battery_sample_t battery;

    bool detail_page;
    uint32_t last_draw_ms;
    uint32_t last_flush_ms;
} dashboard_state_t;

static const char *TAG = "local_dashboard";
static const char *s_weekdays[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};

static QueueHandle_t s_event_queue;
static esp_console_repl_t *s_repl;

static void draw_centered(const char *text, int16_t y, uint8_t scale)
{
    const int16_t x =
        (board_display_width() - mono_gfx_text_width(text, scale)) / 2;
    mono_gfx_draw_text(x, y, text, BOARD_DISPLAY_BLACK, scale);
}

static void draw_info_box(
    int16_t x, int16_t y, int16_t width, int16_t height,
    const char *caption, const char *value)
{
    mono_gfx_draw_rect(x, y, width, height, BOARD_DISPLAY_BLACK);
    mono_gfx_draw_text(
        x + 8, y + 8, caption, BOARD_DISPLAY_BLACK, 1);
    mono_gfx_draw_text(
        x + 8, y + 22, value, BOARD_DISPLAY_BLACK, 2);
}

static void draw_overview_page(const dashboard_state_t *state)
{
    board_display_clear(BOARD_DISPLAY_WHITE);

    char text[48];
    if (state->rtc_valid) {
        snprintf(text, sizeof(text), "%02u:%02u:%02u",
                 (unsigned)state->now.hour,
                 (unsigned)state->now.minute,
                 (unsigned)state->now.second);
    } else {
        snprintf(text, sizeof(text), "--:--:--");
    }
    draw_centered(text, 46, 4);

    if (state->rtc_valid) {
        snprintf(text, sizeof(text), "%04u-%02u-%02u  %s",
                 (unsigned)state->now.year,
                 (unsigned)state->now.month,
                 (unsigned)state->now.day,
                 s_weekdays[state->now.weekday % 7]);
    } else {
        snprintf(text, sizeof(text), "RTC TIME UNAVAILABLE");
    }
    draw_centered(text, 86, 1);

    char temperature[16] = "--";
    char humidity[16] = "--";
    if (state->environment_valid) {
        snprintf(temperature, sizeof(temperature), "%.1fC",
                 (double)state->environment.temperature_c);
        snprintf(humidity, sizeof(humidity), "%.0f%%",
                 (double)state->environment.humidity_percent);
    }
    draw_info_box(30, 120, 160, 60, "TEMP", temperature);
    draw_info_box(210, 120, 160, 60, "HUMI", humidity);

    if (state->rtc_oscillator_stopped) {
        mono_gfx_draw_text(
            8, 8, "RTC TIME UNRELIABLE", BOARD_DISPLAY_BLACK, 1);
    }

    if (state->battery_valid) {
        snprintf(text, sizeof(text), "BAT %u%% %.2fV",
                 (unsigned)state->battery.estimated_percent,
                 (double)state->battery.battery_voltage);
    } else {
        snprintf(text, sizeof(text), "BAT --");
    }
    mono_gfx_draw_text(
        8, board_display_height() - 20,
        text, BOARD_DISPLAY_BLACK, 1);
    mono_gfx_draw_rect(
        board_display_width() - 108, board_display_height() - 22,
        100, 14, BOARD_DISPLAY_BLACK);
    if (state->battery_valid) {
        mono_gfx_fill_rect(
            board_display_width() - 106, board_display_height() - 20,
            state->battery.estimated_percent, 10, BOARD_DISPLAY_BLACK);
    }
}

static void draw_detail_page(const dashboard_state_t *state)
{
    /* The current frame is not complete yet; last_draw/flush show statistics from the previous refresh. */
    board_display_clear(BOARD_DISPLAY_WHITE);
    mono_gfx_draw_text(
        16, 20, "DETAIL / STATS", BOARD_DISPLAY_BLACK, 2);

    char text[48];
    if (state->battery_valid) {
        snprintf(text, sizeof(text), "battery   : %.3f V (%u%%)",
                 (double)state->battery.battery_voltage,
                 (unsigned)state->battery.estimated_percent);
    } else {
        snprintf(text, sizeof(text), "battery   : unavailable");
    }
    mono_gfx_draw_text(16, 64, text, BOARD_DISPLAY_BLACK, 1);

    if (state->environment_valid) {
        snprintf(text, sizeof(text), "temp      : %.2f C",
                 (double)state->environment.temperature_c);
        mono_gfx_draw_text(16, 82, text, BOARD_DISPLAY_BLACK, 1);
        snprintf(text, sizeof(text), "humidity  : %.2f %%RH",
                 (double)state->environment.humidity_percent);
        mono_gfx_draw_text(16, 100, text, BOARD_DISPLAY_BLACK, 1);
    } else {
        mono_gfx_draw_text(
            16, 82, "env       : sensor error", BOARD_DISPLAY_BLACK, 1);
    }

    snprintf(text, sizeof(text), "uptime    : %lld s",
             esp_timer_get_time() / 1000000);
    mono_gfx_draw_text(16, 126, text, BOARD_DISPLAY_BLACK, 1);
    snprintf(text, sizeof(text), "last draw : %lu ms",
             (unsigned long)state->last_draw_ms);
    mono_gfx_draw_text(16, 152, text, BOARD_DISPLAY_BLACK, 1);
    snprintf(text, sizeof(text), "last flush: %lu ms",
             (unsigned long)state->last_flush_ms);
    mono_gfx_draw_text(16, 170, text, BOARD_DISPLAY_BLACK, 1);

    mono_gfx_draw_text(
        16, 204, "KEY: switch page", BOARD_DISPLAY_BLACK, 1);
    mono_gfx_draw_text(
        16, 222, "BOOT: force refresh", BOARD_DISPLAY_BLACK, 1);
}

static void refresh_display(dashboard_state_t *state)
{
    if (!state->display_ready) {
        return;
    }

    int64_t started_at = esp_timer_get_time();
    if (state->detail_page) {
        draw_detail_page(state);
    } else {
        draw_overview_page(state);
    }
    state->last_draw_ms =
        (uint32_t)((esp_timer_get_time() - started_at) / 1000);

    started_at = esp_timer_get_time();
    /* board_display_flush waits for transfer completion, so this timing includes SPI transmission. */
    const esp_err_t err = board_display_flush();
    state->last_flush_ms =
        (uint32_t)((esp_timer_get_time() - started_at) / 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display flush failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "refresh: draw=%lu ms, flush=%lu ms",
             (unsigned long)state->last_draw_ms,
             (unsigned long)state->last_flush_ms);
}

static void sample_fast_inputs(dashboard_state_t *state)
{
    if (state->rtc_present) {
        pcf85063_datetime_t current = {0};
        bool oscillator_stopped = false;
        const esp_err_t err = pcf85063_read(&current, &oscillator_stopped);
        state->rtc_oscillator_stopped = oscillator_stopped;
        state->rtc_valid = err == ESP_OK && !oscillator_stopped &&
                           pcf85063_datetime_is_valid(&current);
        if (state->rtc_valid) {
            state->now = current;
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "RTC read failed: %s", esp_err_to_name(err));
        }
    }

    if (state->battery_ready) {
        const esp_err_t err = board_battery_read(
            BATTERY_SAMPLE_COUNT, &state->battery);
        state->battery_valid = err == ESP_OK;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "battery read failed: %s", esp_err_to_name(err));
        }
    }
}

static void sample_environment(dashboard_state_t *state)
{
    if (!state->environment_present) {
        return;
    }

    const esp_err_t err = shtc3_measure(&state->environment);
    state->environment_valid = err == ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SHTC3 measurement failed: %s", esp_err_to_name(err));
    }
}

static void set_rtc(
    dashboard_state_t *state, const pcf85063_datetime_t *requested)
{
    if (!state->rtc_present) {
        printf("RTC is not available\n");
        return;
    }

    esp_err_t err = pcf85063_write(requested);
    if (err != ESP_OK) {
        printf("RTC write failed: %s\n", esp_err_to_name(err));
        state->rtc_valid = false;
        return;
    }

    pcf85063_datetime_t verified = {0};
    bool oscillator_stopped = true;
    err = pcf85063_read(&verified, &oscillator_stopped);
    state->rtc_oscillator_stopped = oscillator_stopped;
    state->rtc_valid = err == ESP_OK && !oscillator_stopped &&
                       pcf85063_datetime_is_valid(&verified);
    if (state->rtc_valid) {
        state->now = verified;
        printf("RTC set OK -> %04u-%02u-%02u %02u:%02u:%02u\n",
               (unsigned)verified.year, (unsigned)verified.month,
               (unsigned)verified.day, (unsigned)verified.hour,
               (unsigned)verified.minute, (unsigned)verified.second);
    } else {
        printf("RTC write completed, but read-back validation failed\n");
    }
}

static bool parse_datetime(
    const char *date_text,
    const char *time_text,
    pcf85063_datetime_t *datetime)
{
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    char trailing = '\0';

    if (date_text == NULL || time_text == NULL || datetime == NULL ||
        sscanf(date_text, "%d-%d-%d%c", &year, &month, &day, &trailing) != 3 ||
        sscanf(time_text, "%d:%d:%d%c", &hour, &minute, &second, &trailing) != 3 ||
        year < 2000 || year > 2099 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }

    *datetime = (pcf85063_datetime_t) {
        .year = (uint16_t)year,
        .month = (uint8_t)month,
        .day = (uint8_t)day,
        .hour = (uint8_t)hour,
        .minute = (uint8_t)minute,
        .second = (uint8_t)second,
    };
    datetime->weekday = pcf85063_calculate_weekday(
        datetime->year, datetime->month, datetime->day);
    return pcf85063_datetime_is_valid(datetime);
}

static int set_time_command(int argc, char **argv)
{
    /* Parse input in the console task; success only means queued, with the actual time-setting result logged later. */
    if (argc != 3) {
        printf("Usage: rtc-set YYYY-MM-DD HH:MM:SS\n");
        return 1;
    }

    app_event_t event = {.type = APP_EVENT_SET_RTC};
    if (!parse_datetime(argv[1], argv[2], &event.datetime)) {
        printf("Invalid date/time. Example: rtc-set 2026-08-26 12:34:56\n");
        return 1;
    }
    if (xQueueSend(s_event_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
        printf("Dashboard is busy; try again\n");
        return 1;
    }
    return 0;
}

static esp_err_t start_console(void)
{
    esp_err_t err = esp_console_register_help_command();
    if (err != ESP_OK) {
        return err;
    }

    const esp_console_cmd_t command = {
        .command = "rtc-set",
        .help = "Set RTC time: rtc-set YYYY-MM-DD HH:MM:SS",
        .func = set_time_command,
    };
    err = esp_console_cmd_register(&command);
    if (err != ESP_OK) {
        return err;
    }

    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "dashboard> ";
    repl_config.max_cmdline_length = 64;

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || \
    defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    const esp_console_dev_uart_config_t hardware_config =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    err = esp_console_new_repl_uart(
        &hardware_config, &repl_config, &s_repl);
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    const esp_console_dev_usb_cdc_config_t hardware_config =
        ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    err = esp_console_new_repl_usb_cdc(
        &hardware_config, &repl_config, &s_repl);
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    const esp_console_dev_usb_serial_jtag_config_t hardware_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    err = esp_console_new_repl_usb_serial_jtag(
        &hardware_config, &repl_config, &s_repl);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif

    return err == ESP_OK ? esp_console_start_repl(s_repl) : err;
}

static void post_simple_event(app_event_type_t type)
{
    /* Neither timer-service nor button tasks wait for queue space; drop and report full-queue events to avoid blocking the source. */
    const app_event_t event = {.type = type};
    if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropped event %d", type);
    }
}

static void timer_callback(TimerHandle_t timer)
{
    /* A software-timer callback is not an ISR, but must not sample, refresh the screen, or wait for long here. */
    post_simple_event((app_event_type_t)(uintptr_t)pvTimerGetTimerID(timer));
}

static void button_callback(button_service_event_t event, void *context)
{
    (void)context;
    if (event == BUTTON_SERVICE_EVENT_USER_PRESSED) {
        post_simple_event(APP_EVENT_USER_PRESSED);
    } else if (event == BUTTON_SERVICE_EVENT_BOOT_PRESSED) {
        post_simple_event(APP_EVENT_BOOT_PRESSED);
    }
}

static void start_event_sources(void)
{
    const button_service_config_t button_config =
        BUTTON_SERVICE_CONFIG_DEFAULT(button_callback, NULL);
    ESP_ERROR_CHECK(button_service_start(&button_config));

    TimerHandle_t fast_timer = xTimerCreate(
        "dashboard_fast", pdMS_TO_TICKS(FAST_REFRESH_MS), pdTRUE,
        (void *)(uintptr_t)APP_EVENT_FAST_TICK, timer_callback);
    TimerHandle_t environment_timer = xTimerCreate(
        "dashboard_env", pdMS_TO_TICKS(ENVIRONMENT_REFRESH_MS), pdTRUE,
        (void *)(uintptr_t)APP_EVENT_ENVIRONMENT_TICK, timer_callback);
    if (fast_timer == NULL || environment_timer == NULL) {
        ESP_LOGE(TAG, "failed to create dashboard timers");
        return;
    }
    ESP_ERROR_CHECK(xTimerStart(fast_timer, 0) == pdPASS
                        ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTimerStart(environment_timer, 0) == pdPASS
                        ? ESP_OK : ESP_ERR_NO_MEM);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[07] local dashboard");
    s_event_queue = xQueueCreate(12, sizeof(app_event_t));
    ESP_ERROR_CHECK(s_event_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    dashboard_state_t state = {0};
    const board_display_config_t display_config =
        BOARD_DISPLAY_CONFIG_DEFAULT();
    esp_err_t err = board_display_init(&display_config);
    state.display_ready = err == ESP_OK;
    if (!state.display_ready) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(err));
    }

    err = board_i2c_init();
    if (err == ESP_OK) {
        state.rtc_present =
            pcf85063_init(board_i2c_get_handle()) == ESP_OK;
        state.environment_present =
            shtc3_init(board_i2c_get_handle()) == ESP_OK;
    } else {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
    }
    state.battery_ready = board_battery_init() == ESP_OK;
    ESP_LOGI(TAG, "RTC: %s, SHTC3: %s, battery ADC: %s",
             state.rtc_present ? "ready" : "missing",
             state.environment_present ? "ready" : "missing",
             state.battery_ready ? "ready" : "failed");

    sample_fast_inputs(&state);
    sample_environment(&state);
    refresh_display(&state);

    start_event_sources();
    err = start_console();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "console unavailable: %s", esp_err_to_name(err));
    } else {
        printf("Use: rtc-set YYYY-MM-DD HH:MM:SS\n");
    }

    for (;;) {
        app_event_t event;
        /* Serialize all I2C, ADC, and drawing operations; block for events rather than busy-polling. */
        if (xQueueReceive(s_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        bool redraw = false;
        switch (event.type) {
        case APP_EVENT_FAST_TICK:
            sample_fast_inputs(&state);
            /* Keep updating cached readings on the detail page, but do not redraw it for every clock tick. */
            redraw = !state.detail_page;
            break;
        case APP_EVENT_ENVIRONMENT_TICK:
            sample_environment(&state);
            redraw = true;
            break;
        case APP_EVENT_USER_PRESSED:
            state.detail_page = !state.detail_page;
            redraw = true;
            break;
        case APP_EVENT_BOOT_PRESSED:
            /* Force a redraw from the existing cache without changing sensor sampling intervals. */
            redraw = true;
            break;
        case APP_EVENT_SET_RTC:
            set_rtc(&state, &event.datetime);
            redraw = true;
            break;
        default:
            break;
        }
        if (redraw) {
            refresh_display(&state);
        }
    }
}
