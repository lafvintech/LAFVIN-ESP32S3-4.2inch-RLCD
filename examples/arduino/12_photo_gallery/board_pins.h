#pragma once

// ============================================================
// board_pins.h -- Single source of board pin definitions (keep aligned with HARDWARE.md)
// ============================================================

// ---- Display (ST7305, SPI3) ----
#define PIN_LCD_MOSI 12
#define PIN_LCD_SCK 11
#define PIN_LCD_DC 5
#define PIN_LCD_CS 40
#define PIN_LCD_RST 41
#define PIN_LCD_TE 6

// Logical display resolution
#define LCD_WIDTH_LANDSCAPE 400
#define LCD_HEIGHT_LANDSCAPE 300

// ---- Shared I2C bus ----
#define PIN_I2C_SDA 13
#define PIN_I2C_SCL 14
#define I2C_ADDR_PCF85063 0x51
#define I2C_ADDR_SHTC3 0x70
#define I2C_ADDR_ES8311 0x18
#define I2C_ADDR_ES7210 0x40

// ---- Audio I2S + amplifier ----
// The S3_RLCD_4_2 board profile in espressif/codec_board manages the actual pins.
// Listed here for reference only; code does not need to reference these manually:
//   MCLK=16  BCLK=9  WS=45  DIN(recording)=10  DOUT(playback)=8  PA enable=46

// ---- Buttons (active low) ----
#define PIN_KEY_BOOT 0
#define PIN_KEY_USER 18

// ---- SDMMC 1-bit recording storage ----
#define PIN_SD_CLK 38
#define PIN_SD_CMD 21
#define PIN_SD_D0 39

// ---- Battery ADC: external 1:3 divider ----
#define PIN_BATTERY_ADC 4

#define BATTERY_DIVIDER_RATIO 3.0f
