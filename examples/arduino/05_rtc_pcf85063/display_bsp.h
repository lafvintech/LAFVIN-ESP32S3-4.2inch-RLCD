#pragma once

// ============================================================
// display_bsp.h -- Custom ST7305 reflective always-on display driver (no U8g2 dependency)
//
// Design notes:
//   * The panel is 1bpp: each framebuffer byte holds 8 pixels (see the packing notes at the top of the .cpp).
//   * This class maintains a full-screen framebuffer. setPixel() only updates memory;
//     flush() sends the entire frame to the display in one transfer.
//   * Supports this board's 400x300 / 300x400 orientations only, not arbitrary resolutions.
//   * A single caller owns the panel and framebuffer; no multitasking drawing/flush lock is provided.
// ============================================================

#include <stdint.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>

// Pixel values: framebuffer 1 = black (dark on the reflective display), 0 = white.
enum LcdPixelValue : uint8_t {
  LCD_PIXEL_WHITE = 0x00,
  LCD_PIXEL_BLACK = 0x01,
};

class RlcdPanel {
 public:
  // width/height specify the logical resolution:
  //   Landscape: (400, 300)   Portrait: (300, 400)
  RlcdPanel(int pinMosi, int pinSck, int pinDc, int pinCs, int pinRst,
            int16_t width, int16_t height,
            spi_host_device_t spiHost = SPI3_HOST);

  // The constructor creates the SPI bus and panel IO; ESP_ERROR_CHECK reports failures.
  // Call begin() only once: allocate PSRAM -> reset -> initialize registers -> clear the buffer to white.
  // Returns false if framebuffer allocation fails. No flush here; the caller flushes the first image.
  bool begin();

  bool isReady() const { return frameBuffer_ != nullptr; }
  int16_t width() const { return width_; }
  int16_t height() const { return height_; }

  // Write logical coordinates (x,y) to the framebuffer. Out-of-bounds coordinates are ignored safely.
  void setPixel(int16_t x, int16_t y, uint8_t color);

  // Fill the entire framebuffer with one color (LCD_PIXEL_WHITE / LCD_PIXEL_BLACK).
  void clearBuffer(uint8_t color);

  // Set the full-screen window and submit an SPI transfer; no partial update or completion wait.
  // This example uses infrequent updates/inter-frame waits to avoid overwriting data in flight; do not share it across tasks directly.
  void flush();

  // Low-level transfers, exposed to demonstrate individual commands.
  void sendCommand(uint8_t cmd);
  void sendData(uint8_t data);

 private:
  void hardReset();
  void applyPowerOnInitSequence();
  void pushFrameWindow();

  // Pins and geometry
  int pinDc_;
  int pinCs_;
  int pinRst_;
  int16_t width_;
  int16_t height_;
  bool landscapeMode_;      // width > height

  // ESP-IDF LCD panel IO handle (used for both commands and data)
  esp_lcd_panel_io_handle_t ioHandle_;

  // Framebuffer (SPIRAM): width*height/8 bytes
  uint8_t *frameBuffer_;
  uint32_t frameBytes_;

  // Address window (calculated from the resolution in begin())
  uint8_t winColStart_;
  uint8_t winColEnd_;
  uint8_t winPageStart_;
  uint8_t winPageEnd_;
};

