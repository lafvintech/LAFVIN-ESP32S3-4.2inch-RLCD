// 01 battery ADC: setup() initializes; loop() samples periodically and prints to Serial.
// Reading path: this file (sampling schedule) -> battery_bsp.cpp (sampling, calibration, conversion).
// GPIO4 measures the divided voltage; multiply by 3 for battery voltage. See board_pins.h.

#include <Arduino.h>
#include "battery_bsp.h"

namespace {
constexpr uint32_t kPrintIntervalMs = 1000;
uint32_t lastPrintMs = 0;

void printSample() {
  const BatterySample sample = batteryRead(16);
  if (!sample.ok) {
    Serial.println(F("ADC read failed"));
    return;
  }

  Serial.printf("raw average : %u\n", sample.rawAverage);
  Serial.printf("pin voltage : %u mV\n", sample.pinMillivolts);
  Serial.printf("battery     : %.3f V\n", (double)sample.batteryVoltage);
  Serial.printf("level       : %u %% (rough)\n\n",
                sample.estimatedPercent);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("[01] battery ADC"));
  Serial.println(F("GPIO4 / ADC1_CH3 / divider 1:3\n"));
  batteryBegin();
  printSample();
  lastPrintMs = millis();
}

void loop() {
  const uint32_t now = millis();
  // Use unsigned elapsed time so the interval check remains valid when millis() wraps.
  if (now - lastPrintMs >= kPrintIntervalMs) {
    lastPrintMs = now;
    printSample();
  }
  delay(10);
}

