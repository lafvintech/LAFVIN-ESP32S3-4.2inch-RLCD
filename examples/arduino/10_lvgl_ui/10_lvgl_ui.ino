// ============================================================
// 10_lvgl_ui -- Using LVGL 9 on an ST7305 monochrome display
//
// This example introduces LVGL 9 through a simple two-page interface.
//   Page 1: Label, Button, Checkbox, Switch, Slider, and Bar
//   Page 2: Two 400x300 photos, switching automatically every 3 seconds
// KEY (GPIO18) switches between pages; display output remains I1.
// This board has no touch input, so LVGL timers update the widgets on page 1.
// Reading order: setup initializes -> loop handles the button/LVGL -> ui_bsp builds pages;
// lvgl_port converts pixel layout; display_bsp sends data. UI code does not access SPI.
// Only the user button on GPIO18 is initialized;
// RTC, environment sensors, battery, and other I2C peripherals are not initialized, avoiding unrelated peripherals during porting and troubleshooting.
//
// Dependency setup (see VERSIONS.md / docs/10):
//   1. Install LVGL 9.3.0 through Library Manager.
//   2. Copy this directory's lv_conf.h to <sketchbook>/libraries/
//      (beside the lvgl directory, as required by the Arduino convention).
// ============================================================

#include <Arduino.h>
#include <lvgl.h>
#include "board_pins.h"
#include "buttons_bsp.h"
#include "display_bsp.h"
#include "lvgl_port.h"
#include "ui_bsp.h"

namespace {
RlcdPanel panel(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC,
                PIN_LCD_CS, PIN_LCD_RST,
                LCD_WIDTH_LANDSCAPE, LCD_HEIGHT_LANDSCAPE);
DebouncedButton userButton(PIN_KEY_USER);
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("[10] LVGL9 on self-made ST7305 driver"));
  userButton.begin();

  if (!panel.begin()) {
    Serial.println(F("PANEL_INIT: FAIL"));
    while (true) delay(1000);
  }
  if (!lvglPortBegin(&panel)) {
    Serial.println(F("LVGL init: FAIL"));
    while (true) delay(1000);
  }

  lvglDemoBuild();

  Serial.println(F("[10] UI ready"));
  Serial.println(F("[10] KEY (GPIO18): switch widgets/photo"));
}

void loop() {
  userButton.update();
  if (userButton.consumePress()) {
    // Runs in the same Arduino loop as lv_timer_handler(), so no mutex is needed.
    lvglDemoNextPage();
  }

  // lv_timer_handler() advances LVGL timers, layout, and redraws.
  lvglPortTask(20);
}
