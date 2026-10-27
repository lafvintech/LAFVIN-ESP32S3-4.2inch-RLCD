#pragma once

// ============================================================
// display_bsp.h -- Custom ST7305 reflective always-on display driver (no U8g2 dependency)
//
// Design notes:
//   * The panel is 1bpp: each display-RAM byte stores 8 pixels (see packing notes at the top of the .cpp).
//   * This class maintains a full-screen framebuffer. setPixel only updates that buffer;
//     flush() sends the entire frame to the panel.
//   * Supply logical dimensions at construction: landscape 400x300 / portrait 300x400;
//     the corresponding pixel-packing algorithm is selected automatically.
// ============================================================

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>

// Pixel semantics: framebuffer 1 = black (dark on the reflective display), 0 = white
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

  // Full startup: SPI bus -> panel I/O -> hardware reset -> register sequence -> clear to white
  // Returns false if any stage fails (for example, framebuffer allocation).
  bool begin();

  bool isReady() const { return frameBuffer_ != nullptr; }
  int16_t width() const { return width_; }
  int16_t height() const { return height_; }

  // Write logical (x,y) to the framebuffer. Out-of-bounds coordinates are ignored safely.
  void setPixel(int16_t x, int16_t y, uint8_t color);

  // Fill the entire framebuffer with one color (LCD_PIXEL_WHITE / LCD_PIXEL_BLACK)
  void clearBuffer(uint8_t color);

  // Send the full framebuffer to the display (set the address window, then write RAM sequentially)
  bool flush();

  // Low-level I/O: exposed so individual commands can be demonstrated
  void sendCommand(uint8_t cmd);
  void sendData(uint8_t data);

 private:
  void hardReset();
  bool initIo();
  static bool colorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *context);
  int pinMosi_, pinSck_;
  spi_host_device_t spiHost_;
  SemaphoreHandle_t transferDone_ = nullptr;
  bool ioError_ = false;
  void applyPowerOnInitSequence();
  void pushFrameWindow();

  // Pins and geometry
  int pinDc_;
  int pinCs_;
  int pinRst_;
  int16_t width_;
  int16_t height_;
  bool landscapeMode_;      // width > height

  // ESP-IDF LCD panel I/O handle (used for both commands and data)
  esp_lcd_panel_io_handle_t ioHandle_;

  // Framebuffer (SPIRAM): width*height/8 bytes
  uint8_t *frameBuffer_;
  uint32_t frameBytes_;

  // Address window (derived from resolution in begin)
  uint8_t winColStart_;
  uint8_t winColEnd_;
  uint8_t winPageStart_;
  uint8_t winPageEnd_;
};
