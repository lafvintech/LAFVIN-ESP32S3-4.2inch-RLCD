/* 01 Battery ADC: app_main schedules sampling; board_adc handles calibration and voltage-divider scaling.
  * app_main already runs in a FreeRTOS task; this example needs no additional sampling task. */
#include <stdio.h>

#include "board_adc.h"
#include "board_pins.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BATTERY_SAMPLE_COUNT 16
#define PRINT_INTERVAL_MS    1000

static void print_sample(void)
{
    board_battery_sample_t sample = {0};
    const esp_err_t err = board_battery_read(BATTERY_SAMPLE_COUNT, &sample);
    if (err != ESP_OK) {
        printf("ADC read failed: %s\n", esp_err_to_name(err));
        return;
    }

    printf("raw average : %u\n", sample.raw_average);
    printf("pin voltage : %u mV\n", sample.pin_millivolts);
    printf("battery     : %.3f V\n", (double)sample.battery_voltage);
    printf("level       : %u %% (rough)\n\n", sample.estimated_percent);
}

void app_main(void)
{
    printf("[01] battery ADC\n");
    printf("GPIO4 / ADC1_CH3 / divider 1:3\n\n");

    ESP_ERROR_CHECK(board_battery_init());
    for (;;) {
        print_sample();
        /* Wait 1 second after sampling, so the rate is not exactly 1 Hz; yield the CPU while waiting. */
        vTaskDelay(pdMS_TO_TICKS(PRINT_INTERVAL_MS));
    }
}

