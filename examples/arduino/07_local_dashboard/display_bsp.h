#pragma once

// ============================================================
// display_bsp.h -- custom ST7305 driver (lookup-table pixel packing)
//
// Compared with chapter 03: retain direct-calculation setPixelSlow() and add lookup-based setPixel().
// Precompute byte indices and bit masks during initialization; both tables total 360000 bytes in PSRAM.
// Direct calculation mainly uses shifts; lookup is not guaranteed faster. The startup benchmark compares this environment.
// ============================================================

#include <stdint.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>

enum LcdPixelValue : uint8_t {
  LCD_PIXEL_WHITE = 0x00,
  LCD_PIXEL_BLACK = 0x01,
};

class RlcdPanel {
 public:
  RlcdPanel(int pinMosi, int pinSck, int pinDc, int pinCs, int pinRst,
            int16_t width, int16_t height,
            spi_host_device_t spiHost = SPI3_HOST);

  // SPI/panel IO is created in the constructor. Call begin only once to allocate the buffer/tables,
  // reset/initialize the panel and clear the white buffer, without sending a frame. Do not draw after a false result.
  bool begin();
  bool isReady() const { return frameBuffer_ != nullptr; }
  int16_t width() const { return width_; }
  int16_t height() const { return height_; }

  void setPixel(int16_t x, int16_t y, uint8_t color);       // Lookup.
  void setPixelSlow(int16_t x, int16_t y, uint8_t color);   // Direct calculation (chapter 03).

  void clearBuffer(uint8_t color);
  // Submit a full-frame asynchronous transfer without waiting for SPI; use one caller at low frequency, with no concurrent buffer writes.
  void flush();

  void sendCommand(uint8_t cmd);
  void sendData(uint8_t data);

 private:
  void hardReset();
  void applyPowerOnInitSequence();
  bool buildLookupTables();

  int pinRst_;
  int16_t width_;
  int16_t height_;
  bool landscapeMode_;

  esp_lcd_panel_io_handle_t ioHandle_;

  uint8_t *frameBuffer_;
  uint32_t frameBytes_;

  // Lookup tables (SPIRAM, row-major: index = y * width + x).
  uint16_t *lutByteIndex_;   // Framebuffer byte index for this pixel.
  uint8_t *lutBitMask_;      // Bit mask within that byte.

  uint8_t winColStart_;
  uint8_t winColEnd_;
  uint8_t winPageStart_;
  uint8_t winPageEnd_;
};
