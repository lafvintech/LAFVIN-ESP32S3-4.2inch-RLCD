#include <Arduino.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "board_pins.h"
#include "battery_bsp.h"

static adc_oneshot_unit_handle_t s_adcUnit = nullptr;
static adc_cali_handle_t s_adcCali = nullptr;

bool batteryInit() {
  if (s_adcUnit) return true;

  // Calibration must match the channel attenuation and bit width; this component retains its handles until restart.
  adc_cali_curve_fitting_config_t caliCfg = {};
  caliCfg.unit_id = ADC_UNIT_1;
  caliCfg.chan = ADC_CHANNEL_3;
  caliCfg.atten = ADC_ATTEN_DB_12;
  caliCfg.bitwidth = ADC_BITWIDTH_12;
  if (ESP_OK != adc_cali_create_scheme_curve_fitting(&caliCfg, &s_adcCali)) {
    return false;
  }

  adc_oneshot_unit_init_cfg_t unitCfg = {};
  unitCfg.unit_id = ADC_UNIT_1;
  unitCfg.ulp_mode = ADC_ULP_MODE_DISABLE;
  if (ESP_OK != adc_oneshot_new_unit(&unitCfg, &s_adcUnit)) {
    return false;
  }

  adc_oneshot_chan_cfg_t chanCfg = {};
  chanCfg.bitwidth = ADC_BITWIDTH_12;
  chanCfg.atten = ADC_ATTEN_DB_12;
  return ESP_OK == adc_oneshot_config_channel(s_adcUnit, ADC_CHANNEL_3, &chanCfg);
}

float batteryReadVoltage() {
  if (!s_adcUnit) return -1.0f;

  // Average multiple samples to suppress isolated spikes.
  uint32_t sumMv = 0;
  const int samples = 16;
  int goodSamples = 0;
  for (int i = 0; i < samples; i++) {
    int raw = 0;
    if (ESP_OK != adc_oneshot_read(s_adcUnit, ADC_CHANNEL_3, &raw)) continue;
    int mv = 0;
    if (ESP_OK != adc_cali_raw_to_voltage(s_adcCali, raw, &mv)) continue;
    sumMv += mv;
    goodSamples++;
  }
  // Some samples may fail; if all conversions fail, do not report the result as 0 V.
  if (goodSamples == 0) return -1.0f;

  float pinMv = sumMv / (float)goodSamples;
  return pinMv / 1000.0f * BATTERY_DIVIDER_RATIO;   // Reverse the external 1/3 voltage divider.
}

uint8_t batteryEstimatePercent(float voltageV) {
  const float kEmptyV = 3.00f;
  const float kFullV = 4.12f;
  if (voltageV <= 0) return 0;
  if (voltageV >= kFullV) return 100;
  if (voltageV <= kEmptyV) return 0;
  return (uint8_t)((voltageV - kEmptyV) / (kFullV - kEmptyV) * 100.0f);
}
