#pragma once

#include <stdint.h>

struct BatterySample {
  bool ok;
  uint16_t rawAverage;        // Average raw ADC reading, not a voltage
  uint16_t pinMillivolts;     // Calibrated voltage at the ADC pin, in mV
  float batteryVoltage;      // Battery voltage after compensating for the divider, in V
  uint8_t estimatedPercent;  // Linear voltage-to-percent estimate for approximate display only
};

void batteryBegin();
// Collect samples synchronously, waiting 2 ms per sample; not continuous background sampling.
// Returns ok=false if sampleCount=0 or the pin voltage is 0; the caller must not use the result.
BatterySample batteryRead(uint8_t sampleCount = 16);

