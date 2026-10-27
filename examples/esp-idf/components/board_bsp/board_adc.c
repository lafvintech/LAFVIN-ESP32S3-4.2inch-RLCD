#include "board_adc.h"

#include <string.h>

#include "board_pins.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Empirical display range only, not actual remaining capacity or battery-protection thresholds. */
#define BATTERY_EMPTY_VOLTAGE 3.00f
#define BATTERY_FULL_VOLTAGE  4.12f

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_cali_handle;

static uint8_t estimate_percent(float voltage)
{
    if (voltage <= BATTERY_EMPTY_VOLTAGE) {
        return 0;
    }
    if (voltage >= BATTERY_FULL_VOLTAGE) {
        return 100;
    }

    const float level = (voltage - BATTERY_EMPTY_VOLTAGE) /
                        (BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE) * 100.0f;
    return (uint8_t)level;
}

esp_err_t board_battery_init(void)
{
    if (s_adc_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = BOARD_BATTERY_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_config, &s_adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    /* ADC attenuation extends the input range and is independent of the board's 1/3 resistor-divider ratio. */
    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_oneshot_config_channel(
        s_adc_handle, BOARD_BATTERY_ADC_CHANNEL, &channel_config);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return err;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    /* Calibration must match the sampling channel's attenuation and bit width; do not use raw * 3.3 / 4095. */
    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = BOARD_BATTERY_ADC_UNIT,
        .chan = BOARD_BATTERY_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_cali_create_scheme_curve_fitting(
        &calibration_config, &s_cali_handle);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return err;
    }
#else
    adc_oneshot_del_unit(s_adc_handle);
    s_adc_handle = NULL;
    return ESP_ERR_NOT_SUPPORTED;
#endif

    return ESP_OK;
}

esp_err_t board_battery_read(size_t sample_count, board_battery_sample_t *sample)
{
    if (s_adc_handle == NULL || s_cali_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sample == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(sample, 0, sizeof(*sample));

    uint64_t raw_sum = 0;
    uint64_t millivolt_sum = 0;
    for (size_t i = 0; i < sample_count; ++i) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(
            s_adc_handle, BOARD_BATTERY_ADC_CHANNEL, &raw);
        if (err != ESP_OK) {
            return err;
        }

        /* Unlike the Arduino version, convert the raw value from this same sample to calibrated millivolts. */
        int millivolts = 0;
        err = adc_cali_raw_to_voltage(s_cali_handle, raw, &millivolts);
        if (err != ESP_OK) {
            return err;
        }

        raw_sum += (uint32_t)raw;
        millivolt_sum += (uint32_t)millivolts;
        if (i + 1 < sample_count) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    sample->raw_average = (uint16_t)(raw_sum / sample_count);
    sample->pin_millivolts = (uint16_t)(millivolt_sum / sample_count);
    sample->ok = sample->pin_millivolts > 0;
    if (!sample->ok) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Convert the pin voltage from mV to V, then undo the board resistor network's 1/3 divider ratio. */
    sample->battery_voltage = sample->pin_millivolts / 1000.0f *
                              BOARD_BATTERY_DIVIDER_RATIO;
    sample->estimated_percent = estimate_percent(sample->battery_voltage);
    return ESP_OK;
}

esp_err_t board_battery_deinit(void)
{
    esp_err_t first_error = ESP_OK;

    if (s_cali_handle != NULL) {
        const esp_err_t err = adc_cali_delete_scheme_curve_fitting(s_cali_handle);
        if (first_error == ESP_OK) {
            first_error = err;
        }
        s_cali_handle = NULL;
    }

    if (s_adc_handle != NULL) {
        const esp_err_t err = adc_oneshot_del_unit(s_adc_handle);
        if (first_error == ESP_OK) {
            first_error = err;
        }
        s_adc_handle = NULL;
    }

    return first_error;
}

