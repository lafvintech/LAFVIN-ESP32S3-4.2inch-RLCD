#include <Arduino.h>
#include "board_pins.h"
#include "battery_bsp.h"

namespace {
uint8_t estimatePercent(float voltage) {
  // These empirical display limits are neither a lithium battery discharge curve nor charge/discharge protection thresholds.
  constexpr float kEmptyVoltage = 3.00f;
  constexpr float kFullVoltage = 4.12f;

  if (voltage <= kEmptyVoltage) return 0;
  if (voltage >= kFullVoltage) return 100;
  return (uint8_t)((voltage - kEmptyVoltage) /
                   (kFullVoltage - kEmptyVoltage) * 100.0f);
}
}  // namespace

void batteryBegin() {
  analogReadResolution(12);
  // Attenuation extends the ADC range; it is separate from the onboard divider and does not replace multiplying voltage by 3.
  analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
}

BatterySample batteryRead(uint8_t sampleCount) {
  BatterySample sample = {};
  if (sampleCount == 0) return sample;

  uint32_t rawSum = 0;
  uint32_t millivoltSum = 0;
  for (uint8_t i = 0; i < sampleCount; ++i) {
    // The two APIs sample separately, so raw and mV do not correspond to the same conversion.
    // Convert using calibrated millivolt readings, not by scaling raw with 3.3V / 4095.
    rawSum += analogRead(PIN_BATTERY_ADC);
    millivoltSum += analogReadMilliVolts(PIN_BATTERY_ADC);
    delay(2);
  }

  sample.rawAverage = (uint16_t)(rawSum / sampleCount);
  sample.pinMillivolts = (uint16_t)(millivoltSum / sampleCount);
  sample.ok = sample.pinMillivolts > 0;
  if (!sample.ok) return sample;

  sample.batteryVoltage = sample.pinMillivolts / 1000.0f *
                          BATTERY_DIVIDER_RATIO;
  sample.estimatedPercent = estimatePercent(sample.batteryVoltage);
  return sample;
}

