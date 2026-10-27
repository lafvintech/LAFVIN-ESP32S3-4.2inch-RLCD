#pragma once

#include <stdint.h>

// ============================================================
// battery_bsp -- battery voltage measurement (ADC1_CH3, external 1/3 voltage divider)
//
// Use ESP-IDF oneshot ADC with curve-fitting calibration:
//   ESP32-S3 ADC nonlinearity makes a simple raw-value times coefficient conversion inaccurate.
//   Curve fitting uses factory eFuse data for piecewise correction, improving accuracy.
// ============================================================

bool batteryInit();

// Synchronously read up to 16 samples and average successful conversions only; return negative if all fail.
// Return the battery voltage (V), with the divider reversed, not the ADC pin voltage.
float batteryReadVoltage();

// Approximate battery percentage: linearly map 3.00 V to 0% and 4.12 V to 100%.
// Actual remaining lithium-battery capacity is not linear with voltage.
// This percentage is for display only, not charge/discharge protection thresholds.
uint8_t batteryEstimatePercent(float voltageV);
