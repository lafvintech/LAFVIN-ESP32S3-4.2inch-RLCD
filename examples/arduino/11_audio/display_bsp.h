#pragma once

// ============================================================
// display_bsp.h -- ST7305 pixel packing and SPI transfer
//
// Reuses example 03's pixel mapping and example 07's lookup tables; no benchmark is run here.
// setPixel() uses tables to locate bytes/bits; setPixelSlow() retains direct calculation for comparison.
// Tables require extra PSRAM; speed also depends on memory latency, not just operation count.
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

  // The constructor creates the SPI bus. Call begin only once to allocate buffers/tables and initialize the panel.
  bool begin();          // Clears the RAM buffer to white; a later flush performs the first screen update
  bool isReady() const { return frameBuffer_ != nullptr; }
  int16_t width() const { return width_; }
  int16_t height() const { return height_; }

  void setPixel(int16_t x, int16_t y, uint8_t color);       // Lookup-table method
  void setPixelSlow(int16_t x, int16_t y, uint8_t color);   // Direct-calculation method

  void clearBuffer(uint8_t color);
  // Submits an asynchronous SPI transfer, not a completion wait. Do not modify the framebuffer during transfer.
  // This example does not draw concurrently. Faster refresh/multitasking requires completion synchronization or buffer swapping.
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

  // Lookup tables (SPIRAM, row-major: index = y * width + x)
  uint16_t *lutByteIndex_;   // Framebuffer byte index for this pixel
  uint8_t *lutBitMask_;      // Bit mask within that byte

  uint8_t winColStart_;
  uint8_t winColEnd_;
  uint8_t winPageStart_;
  uint8_t winPageEnd_;
};
