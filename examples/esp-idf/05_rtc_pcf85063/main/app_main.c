/* 05 RTC: app_main initializes; the console task sets the time and the refresh task reads it every second.
  * The PCF85063 driver handles registers; commands, time validity, and the simple page stay here for easy tracing.
  * Do not write the build time at startup: backup power should preserve the time last set by the user. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "board_display.h"
#include "board_i2c.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mono_graphics.h"
#include "pcf85063.h"
#include "sdkconfig.h"

#define RTC_REFRESH_INTERVAL_MS 1000

static const char *TAG = "rtc_demo";

static const char *s_weekday_names[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};

static bool s_display_ready;
static bool s_time_valid;
/* After startup, both tasks share the RTC, time-validity state, and display buffer. Protect the entire operation,
  * not just individual I2C transactions, to prevent refreshing during a time change or overlapping drawing operations. */
static SemaphoreHandle_t s_state_mutex;
static esp_console_repl_t *s_repl;

static void print_datetime(
    const pcf85063_datetime_t *datetime, bool oscillator_stopped)
{
    printf("%04u-%02u-%02u %s %02u:%02u:%02u%s\n",
           (unsigned)datetime->year,
           (unsigned)datetime->month,
           (unsigned)datetime->day,
           s_weekday_names[datetime->weekday % 7],
           (unsigned)datetime->hour,
           (unsigned)datetime->minute,
           (unsigned)datetime->second,
           oscillator_stopped ? "  [OS flag: time may be wrong]" : "");
}

static void draw_centered_text(const char *text, int16_t y, uint8_t scale)
{
    const int16_t x =
        (board_display_width() - mono_gfx_text_width(text, scale)) / 2;
    mono_gfx_draw_text(x, y, text, BOARD_DISPLAY_BLACK, scale);
}

static esp_err_t draw_rtc_screen(
    const pcf85063_datetime_t *datetime,
    bool valid,
    const char *status)
{
    if (!s_display_ready) {
        /* Display output is optional; a display failure does not prevent console-based RTC reads or updates. */
        return ESP_OK;
    }

    board_display_clear(BOARD_DISPLAY_WHITE);
    mono_gfx_draw_text(16, 16, "PCF85063 RTC", BOARD_DISPLAY_BLACK, 2);
    mono_gfx_draw_line(
        16, 48, board_display_width() - 17, 48, BOARD_DISPLAY_BLACK);

    if (valid && datetime != NULL) {
        char line[24];
        snprintf(line, sizeof(line), "%02u:%02u:%02u",
                 (unsigned)datetime->hour,
                 (unsigned)datetime->minute,
                 (unsigned)datetime->second);
        draw_centered_text(line, 78, 4);

        snprintf(line, sizeof(line), "%04u-%02u-%02u",
                 (unsigned)datetime->year,
                 (unsigned)datetime->month,
                 (unsigned)datetime->day);
        draw_centered_text(line, 138, 2);
        draw_centered_text(
            s_weekday_names[datetime->weekday % 7], 178, 2);
    } else {
        draw_centered_text("RTC TIME", 82, 3);
        draw_centered_text("UNAVAILABLE", 126, 3);
        draw_centered_text("SET VIA CONSOLE", 184, 2);
    }

    if (status != NULL && status[0] != '\0') {
        draw_centered_text(status, 240, 1);
    }
    draw_centered_text("CMD: rtc-set YYYY-MM-DD HH:MM:SS", 270, 1);
    return board_display_flush();
}

static void log_display_error(esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "display update failed: %s", esp_err_to_name(error));
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

    /* The trailing %c detects extra characters; validate as int before narrowing into the date structure. */
    if (date_text == NULL || time_text == NULL || datetime == NULL ||
        sscanf(date_text, "%d-%d-%d%c",
               &year, &month, &day, &trailing) != 3 ||
        sscanf(time_text, "%d:%d:%d%c",
               &hour, &minute, &second, &trailing) != 3 ||
        year < 2000 || year > 2099 ||
        month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        return false;
    }

    datetime->year = (uint16_t)year;
    datetime->month = (uint8_t)month;
    datetime->day = (uint8_t)day;
    datetime->hour = (uint8_t)hour;
    datetime->minute = (uint8_t)minute;
    datetime->second = (uint8_t)second;
    datetime->weekday = pcf85063_calculate_weekday(
        datetime->year, datetime->month, datetime->day);
    return pcf85063_datetime_is_valid(datetime);
}

/* Runs in the REPL console task, not in an ISR or an app_main polling callback. */
static int set_time_command(int argc, char **argv)
{
    if (argc != 3) {
        printf("Usage: rtc-set YYYY-MM-DD HH:MM:SS\n");
        return 1;
    }

    pcf85063_datetime_t requested = {0};
    if (!parse_datetime(argv[1], argv[2], &requested)) {
        printf("Invalid date/time. Example: rtc-set 2026-08-26 12:34:56\n");
        return 1;
    }

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    esp_err_t err = pcf85063_write(&requested);
    if (err != ESP_OK) {
        s_time_valid = false;
        printf("RTC write failed: %s\n", esp_err_to_name(err));
        log_display_error(draw_rtc_screen(NULL, false, "RTC WRITE FAILED"));
        xSemaphoreGive(s_state_mutex);
        return 1;
    }

    /* A successful I2C write does not establish time validity: read back and check OS and date ranges. */
    pcf85063_datetime_t verified = {0};
    bool oscillator_stopped = true;
    err = pcf85063_read(&verified, &oscillator_stopped);
    if (err == ESP_OK && !oscillator_stopped &&
        pcf85063_datetime_is_valid(&verified)) {
        s_time_valid = true;
        printf("RTC set OK -> ");
        print_datetime(&verified, false);
        log_display_error(draw_rtc_screen(&verified, true, "RTC SET OK"));
        xSemaphoreGive(s_state_mutex);
        return 0;
    }

    s_time_valid = false;
    printf("RTC write completed, but read-back validation failed\n");
    log_display_error(draw_rtc_screen(NULL, false, "RTC VERIFY FAILED"));
    xSemaphoreGive(s_state_mutex);
    return 1;
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
        .hint = NULL,
        .func = set_time_command,
        .argtable = NULL,
    };
    err = esp_console_cmd_register(&command);
    if (err != ESP_OK) {
        return err;
    }

    esp_console_repl_config_t repl_config =
        ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "rtc> ";
    repl_config.max_cmdline_length = 64;

    /* Reuse the console channel selected by sdkconfig instead of initializing another UART on the same peripheral. */
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

    if (err != ESP_OK) {
        return err;
    }
    return esp_console_start_repl(s_repl);
}

static void rtc_refresh_task(void *argument)
{
    (void)argument;
    TickType_t last_wake_time = xTaskGetTickCount();

    for (;;) {
        /* Read at fixed intervals to reduce accumulated processing-time drift; the RTC keeps time independently. */
        vTaskDelayUntil(
            &last_wake_time, pdMS_TO_TICKS(RTC_REFRESH_INTERVAL_MS));
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);

        if (s_time_valid) {
            pcf85063_datetime_t current = {0};
            bool oscillator_stopped = false;
            const esp_err_t err = pcf85063_read(
                &current, &oscillator_stopped);
            if (err == ESP_OK && !oscillator_stopped &&
                pcf85063_datetime_is_valid(&current)) {
                print_datetime(&current, false);
                log_display_error(draw_rtc_screen(
                    &current, true, "UPDATED EACH SECOND"));
            } else {
                /* Stop periodic output when time becomes invalid and wait for rtc-set rather than repeatedly showing an invalid time. */
                s_time_valid = false;
                ESP_LOGW(TAG, "RTC became invalid; periodic output paused");
                printf("Use rtc-set YYYY-MM-DD HH:MM:SS to set it again.\n");
                log_display_error(draw_rtc_screen(
                    NULL, false,
                    err == ESP_OK && oscillator_stopped
                        ? "OS FLAG: SET RTC TIME"
                        : "RTC READ FAILED"));
            }
        }

        xSemaphoreGive(s_state_mutex);
    }
}

static void read_initial_time(void)
{
    pcf85063_datetime_t current = {0};
    bool oscillator_stopped = false;
    const esp_err_t err = pcf85063_read(&current, &oscillator_stopped);

    if (err == ESP_OK) {
        s_time_valid = !oscillator_stopped &&
                       pcf85063_datetime_is_valid(&current);
        if (s_time_valid) {
            printf("RTC time -> ");
            print_datetime(&current, false);
            log_display_error(draw_rtc_screen(
                &current, true, "UPDATED EACH SECOND"));
            return;
        }

        printf("RTC returned an untrusted value -> ");
        print_datetime(&current, oscillator_stopped);
        log_display_error(draw_rtc_screen(
            NULL, false,
            oscillator_stopped
                ? "OS FLAG: SET RTC TIME"
                : "INVALID RTC VALUE"));
    } else {
        s_time_valid = false;
        ESP_LOGE(TAG, "first RTC read failed: %s", esp_err_to_name(err));
        log_display_error(draw_rtc_screen(NULL, false, "RTC READ FAILED"));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "[05] PCF85063 RTC");

    const board_display_config_t display_config =
        BOARD_DISPLAY_CONFIG_DEFAULT();
    esp_err_t err = board_display_init(&display_config);
    s_display_ready = err == ESP_OK;
    ESP_LOGI(TAG, "PANEL_INIT: %s", s_display_ready ? "PASS" : "FAIL");
    if (!s_display_ready) {
        ESP_LOGE(TAG, "panel error: %s", esp_err_to_name(err));
    }

    err = board_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
        log_display_error(draw_rtc_screen(NULL, false, "I2C INIT FAILED"));
        return;
    }

    err = pcf85063_init(board_i2c_get_handle());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC attach failed: %s", esp_err_to_name(err));
        log_display_error(draw_rtc_screen(NULL, false, "RTC ATTACH FAILED"));
        return;
    }

    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        ESP_LOGE(TAG, "failed to create RTC state mutex");
        return;
    }

    /* Console/refresh tasks have not started yet, so the initial read needs no lock. */
    read_initial_time();
    printf("Use: rtc-set YYYY-MM-DD HH:MM:SS\n");

    err = start_console();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console start failed: %s", esp_err_to_name(err));
        return;
    }

    if (xTaskCreate(
            rtc_refresh_task, "rtc_refresh", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create RTC refresh task");
    }
    /* Console and refresh tasks keep running after app_main returns; resources remain allocated until reset. */
}
