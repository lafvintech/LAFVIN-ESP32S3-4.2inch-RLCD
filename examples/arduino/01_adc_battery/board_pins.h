#pragma once

// Battery sensing: GPIO4 = ADC1_CH3. The onboard resistor network divides the battery voltage by 3.
#define PIN_BATTERY_ADC 4
#define BATTERY_DIVIDER_RATIO 3.0f

