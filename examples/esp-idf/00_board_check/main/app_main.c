#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "board_adc.h"
#include "board_buttons.h"
#include "board_i2c.h"
#include "board_pins.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "board_check";

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
    }
}

static const char *wakeup_cause_name(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "UNDEFINED(cold boot)";
    case ESP_SLEEP_WAKEUP_EXT0: return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1: return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER: return "TIMER";
    case ESP_SLEEP_WAKEUP_TOUCHPAD: return "TOUCHPAD";
    case ESP_SLEEP_WAKEUP_ULP: return "ULP";
    default: return "OTHER";
    }
}

static void print_chip_info(void)
{
    esp_chip_info_t chip_info = {0};
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    const esp_err_t flash_err = esp_flash_get_size(NULL, &flash_size);
    const size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    printf("[1] Chip\n");
    printf("  model        : ESP32-S3 rev%d\n", chip_info.revision);
    printf("  cores        : %d\n", chip_info.cores);
    printf("  cpu freq     : %d MHz\n", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    if (flash_err == ESP_OK) {
        printf("  flash size   : %" PRIu32 " KB\n", flash_size / 1024);
    } else {
        printf("  flash size   : unavailable (%s)\n", esp_err_to_name(flash_err));
    }
    printf("  psram size   : %u KB (%s)\n",
           (unsigned)(psram_size / 1024), psram_size > 0 ? "found" : "MISSING");
}

static void print_reset_info(void)
{
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    const esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

    printf("[2] Reset / Wakeup\n");
    printf("  reset reason : %s (%d)\n",
           reset_reason_name(reset_reason), reset_reason);
    printf("  wakeup cause : %s (%d)\n",
           wakeup_cause_name(wakeup_cause), wakeup_cause);
}

static const char *known_i2c_device(uint8_t address)
{
    switch (address) {
    case BOARD_I2C_ADDR_PCF85063: return " <- PCF85063 RTC";
    case BOARD_I2C_ADDR_SHTC3: return " <- SHTC3 sensor";
    case BOARD_I2C_ADDR_ES8311: return " <- ES8311 codec (DAC)";
    case BOARD_I2C_ADDR_ES7210: return " <- ES7210 codec (ADC)";
    default: return "";
    }
}

static int scan_i2c_bus(bool *rtc_ok, bool *shtc3_ok)
{
    *rtc_ok = false;
    *shtc3_ok = false;

    printf("[3] I2C scan (SDA=%d SCL=%d)\n",
           BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO);

    int found = 0;
    for (uint8_t address = 1; address < 0x7F; ++address) {
        if (board_i2c_probe(address) != ESP_OK) {
            continue;
        }

        ++found;
        *rtc_ok |= address == BOARD_I2C_ADDR_PCF85063;
        *shtc3_ok |= address == BOARD_I2C_ADDR_SHTC3;
        printf("  device @ 0x%02X%s\n", address, known_i2c_device(address));
    }

    printf("  summary      : %d device(s); RTC %s, SHTC3 %s\n",
           found,
           *rtc_ok ? "OK" : "not seen",
           *shtc3_ok ? "OK" : "not seen");
    return found;
}

static bool run_self_check(void)
{
    printf("========================================\n");
    printf(" ESP32-S3 RLCD 4.2 - board self check\n");
    printf("========================================\n");

    print_chip_info();
    print_reset_info();

    ESP_ERROR_CHECK(board_buttons_init());

    bool rtc_ok = false;
    bool shtc3_ok = false;
    int i2c_devices = 0;
    esp_err_t i2c_err = board_i2c_init();
    if (i2c_err == ESP_OK) {
        i2c_devices = scan_i2c_bus(&rtc_ok, &shtc3_ok);
        ESP_ERROR_CHECK(board_i2c_deinit());
    } else {
        printf("[3] I2C scan: INIT FAILED (%s)\n", esp_err_to_name(i2c_err));
    }

    printf("[4] Battery\n");
    board_battery_sample_t battery = {0};
    const esp_err_t battery_init_err = board_battery_init();
    const esp_err_t battery_read_err = battery_init_err == ESP_OK
                                           ? board_battery_read(8, &battery)
                                           : battery_init_err;
    if (battery_read_err == ESP_OK) {
        printf("  voltage      : %.2f V (rough)\n", (double)battery.battery_voltage);
    } else {
        printf("  voltage      : READ FAILED (%s)\n",
               esp_err_to_name(battery_read_err));
    }

    const board_button_state_t buttons = board_buttons_read();
    printf("[5] Buttons (low level = pressed)\n");
    printf("  BOOT(GPIO0)  : %s\n", buttons.boot_pressed ? "pressed" : "released");
    printf("  KEY(GPIO18)  : %s\n", buttons.user_pressed ? "pressed" : "released");

    const bool pass = i2c_devices > 0 && battery_read_err == ESP_OK;
    printf("----------------------------------------\n");
    printf("SELF_TEST: %s\n", pass ? "PASS" : "FAIL");
    printf("(i2c devices=%d rtc=%d shtc3=%d)\n",
           i2c_devices, rtc_ok, shtc3_ok);
    return pass;
}

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(300));
    (void)run_self_check();

    bool reset_armed = false;
    for (;;) {
        const board_button_state_t buttons = board_buttons_read();
        if (buttons.user_pressed && !reset_armed) {
            reset_armed = true;
            vTaskDelay(pdMS_TO_TICKS(50));
            if (board_buttons_read().user_pressed) {
                ESP_LOGI(TAG, "KEY pressed; restarting");
                esp_restart();
            }
        } else if (!buttons.user_pressed) {
            reset_armed = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

