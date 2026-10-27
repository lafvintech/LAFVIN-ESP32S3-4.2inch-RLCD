#pragma once

// ============================================================
// board_pins.h -- Single source of board pin definitions (keep in sync with HARDWARE.md)
// ============================================================

// ---- Display (ST7305, SPI3) ----
#define PIN_LCD_MOSI 12
#define PIN_LCD_SCK 11
#define PIN_LCD_DC 5
#define PIN_LCD_CS 40
#define PIN_LCD_RST 41
#define PIN_LCD_TE 6   // Retained in the initialization sequence; this course does not use TE interrupts.

// Native panel: 300x400. Landscape combines scan configuration with setPixel() coordinate mapping.
#define LCD_WIDTH_LANDSCAPE 400
#define LCD_HEIGHT_LANDSCAPE 300
#define LCD_WIDTH_PORTRAIT 300
#define LCD_HEIGHT_PORTRAIT 400

// ---- Shared I2C bus ----
#define PIN_I2C_SDA 13
#define PIN_I2C_SCL 14
#define I2C_ADDR_PCF85063 0x51
#define I2C_ADDR_SHTC3 0x70
#define I2C_ADDR_ES8311 0x18
#define I2C_ADDR_ES7210 0x40

// ---- SD card (SDMMC 1-bit) ----
#define PIN_SD_CLK 38
#define PIN_SD_CMD 21
#define PIN_SD_D0 39

// ---- Buttons (active low) ----
#define PIN_KEY_BOOT 0   // Note: GPIO0 is a strapping pin.
#define PIN_KEY_USER 18

// ---- Battery voltage sensing (external 1/3 divider) ----
#define PIN_BATTERY_ADC 4   // ADC1_CH3
#define BATTERY_DIVIDER_RATIO 3.0f
