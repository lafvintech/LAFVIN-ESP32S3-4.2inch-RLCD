#pragma once

// ============================================================
// board_pins.h -- Display and user-button pin definitions for example 10
// ============================================================

// ---- Display (ST7305, SPI3) ----
#define PIN_LCD_MOSI 12
#define PIN_LCD_SCK 11
#define PIN_LCD_DC 5
#define PIN_LCD_CS 40
#define PIN_LCD_RST 41
// Logical resolution: landscape 400x300 (native panel 300x400, rotated via 0x36)
#define LCD_WIDTH_LANDSCAPE 400
#define LCD_HEIGHT_LANDSCAPE 300

// ---- User button (active low) ----
// GPIO0 is a boot-strapping pin; this example uses only the onboard KEY (GPIO18).
#define PIN_KEY_USER 18
