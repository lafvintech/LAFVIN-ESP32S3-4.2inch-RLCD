#pragma once

#include <stdint.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>

// This example only uses the display, SD card and KEY; all board pins are centralized here.
static constexpr int PIN_LCD_MOSI = 12;
static constexpr int PIN_LCD_SCK = 11;
static constexpr int PIN_LCD_DC = 5;
static constexpr int PIN_LCD_CS = 40;
static constexpr int PIN_LCD_RST = 41;
static constexpr int PIN_SD_CLK = 38;
static constexpr int PIN_SD_CMD = 21;
static constexpr int PIN_SD_D0 = 39;
static constexpr int PIN_KEY = 18;

static constexpr int LCD_WIDTH = 400;
static constexpr int LCD_HEIGHT = 300;

enum LcdPixelValue : uint8_t {
  LCD_PIXEL_WHITE = 0x00,
  LCD_PIXEL_BLACK = 0x01,
};

class RlcdPanel {
 public:
  RlcdPanel(int pinMosi, int pinSck, int pinDc, int pinCs, int pinRst,
            int16_t width, int16_t height,
            spi_host_device_t spiHost = SPI3_HOST);

  // The constructor creates SPI/panel IO; call begin once to allocate PSRAM and initialize the panel.
  // Only the memory buffer is cleared, without refreshing the display; do not draw after a false result.
  bool begin();
  int16_t width() const { return width_; }
  int16_t height() const { return height_; }
  void setPixel(int16_t x, int16_t y, uint8_t color);
  void clearBuffer(uint8_t color);
  // Submit a full-frame asynchronous transfer without waiting; concurrent drawing from multiple tasks is unsupported.
  void flush();
  void sendCommand(uint8_t cmd);
  void sendData(uint8_t data);

 private:
  void hardReset();
  void applyPowerOnInitSequence();
  void pushFrameWindow();

  int pinDc_;
  int pinCs_;
  int pinRst_;
  int16_t width_;
  int16_t height_;
  bool landscapeMode_;
  esp_lcd_panel_io_handle_t ioHandle_;
  uint8_t *frameBuffer_;
  uint32_t frameBytes_;
  uint8_t winColStart_;
  uint8_t winColEnd_;
  uint8_t winPageStart_;
  uint8_t winPageEnd_;
};

// These functions only modify the buffer; origin is top-left, w/h are sizes, and text is transparent ASCII.
void drawLine(RlcdPanel &panel, int16_t x0, int16_t y0,
              int16_t x1, int16_t y1, uint8_t color);
void drawRect(RlcdPanel &panel, int16_t x, int16_t y,
              int16_t w, int16_t h, uint8_t color);
void fillRect(RlcdPanel &panel, int16_t x, int16_t y,
              int16_t w, int16_t h, uint8_t color);
void drawChar(RlcdPanel &panel, int16_t x, int16_t y,
              char ch, uint8_t color, uint8_t scale = 1);
void drawString(RlcdPanel &panel, int16_t x, int16_t y,
                const char *text, uint8_t color, uint8_t scale = 1);

// Polling-based software debounce, without background scanning; consumePress consumes one confirmed press edge.
class DebouncedButton {
 public:
  explicit DebouncedButton(uint8_t pin);
  void begin();
  void update();
  bool consumePress();

 private:
  uint8_t pin_;
  bool rawDown_ = false;
  bool stableDown_ = false;
  bool pressPending_ = false;
  uint32_t changedAtMs_ = 0;
};
