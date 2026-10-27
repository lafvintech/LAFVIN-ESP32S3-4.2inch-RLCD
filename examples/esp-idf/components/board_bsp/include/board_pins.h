#pragma once

/* Board-level pin definitions. Keep these values aligned with HARDWARE.md. */

/* ST7305 display, SPI3 */
#define BOARD_LCD_MOSI_GPIO       12
#define BOARD_LCD_SCLK_GPIO       11
#define BOARD_LCD_DC_GPIO          5
#define BOARD_LCD_CS_GPIO         40
#define BOARD_LCD_RESET_GPIO      41
#define BOARD_LCD_TE_GPIO          6

#define BOARD_LCD_LANDSCAPE_WIDTH   400
#define BOARD_LCD_LANDSCAPE_HEIGHT  300
#define BOARD_LCD_PORTRAIT_WIDTH    300
#define BOARD_LCD_PORTRAIT_HEIGHT   400

/* Shared I2C bus */
#define BOARD_I2C_PORT             0
#define BOARD_I2C_SDA_GPIO        13
#define BOARD_I2C_SCL_GPIO        14
#define BOARD_I2C_FREQUENCY_HZ    400000

#define BOARD_I2C_ADDR_PCF85063   0x51
#define BOARD_I2C_ADDR_SHTC3      0x70
#define BOARD_I2C_ADDR_ES8311     0x18
#define BOARD_I2C_ADDR_ES7210     0x40

/* ES8311 playback and ES7210 capture share one full-duplex I2S bus. */
#define BOARD_AUDIO_MCLK_GPIO     16
#define BOARD_AUDIO_BCLK_GPIO      9
#define BOARD_AUDIO_WS_GPIO       45
#define BOARD_AUDIO_DIN_GPIO      10
#define BOARD_AUDIO_DOUT_GPIO      8
#define BOARD_AUDIO_PA_GPIO       46

/* SDMMC 1-bit */
#define BOARD_SD_CLK_GPIO         38
#define BOARD_SD_CMD_GPIO         21
#define BOARD_SD_D0_GPIO          39

/* Active-low buttons */
#define BOARD_BOOT_BUTTON_GPIO     0
#define BOARD_USER_BUTTON_GPIO    18

/* Battery voltage: GPIO4 = ADC1 channel 3, external divider is 1:3. */
#define BOARD_BATTERY_ADC_UNIT        ADC_UNIT_1
#define BOARD_BATTERY_ADC_CHANNEL     ADC_CHANNEL_3
#define BOARD_BATTERY_DIVIDER_RATIO   3.0f
