#pragma once

// ============================================================
// board.h -- display, RTC, SHTC3 and button interfaces for example 08
//
// Design notes:
//   * The panel is 1bpp: each framebuffer byte holds 8 pixels (see the packing notes at the top of the .cpp).
//   * This class maintains a full-screen framebuffer. setPixel() only updates memory;
//     flush() sends the entire frame to the display in one transfer.
//   * Only this panel's 400x300 / 300x400 logical orientations are supported; this is not a generic-resolution driver.
// ============================================================

#include <stdint.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>

// Fixed hardware settings match HARDWARE.md; edit everyday user settings in app_config.h.
static constexpr int PIN_LCD_MOSI = 12;
static constexpr int PIN_LCD_SCK = 11;
static constexpr int PIN_LCD_DC = 5;
static constexpr int PIN_LCD_CS = 40;
static constexpr int PIN_LCD_RST = 41;
static constexpr int PIN_I2C_SDA = 13;
static constexpr int PIN_I2C_SCL = 14;
static constexpr int PIN_KEY = 18;

static constexpr int LCD_WIDTH = 400;
static constexpr int LCD_HEIGHT = 300;

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

  // The constructor creates SPI/panel IO; ESP_ERROR_CHECK reports failures.
  // Call begin only once: allocate PSRAM -> reset/initialize registers -> clear the buffer to white.
  // No first frame is sent. A false result means buffer allocation failed; do not draw afterward.
  bool begin();

  bool isReady() const { return frameBuffer_ != nullptr; }
  int16_t width() const { return width_; }
  int16_t height() const { return height_; }

  // Write logical coordinates (x,y) to the framebuffer. Out-of-bounds coordinates are ignored safely.
  void setPixel(int16_t x, int16_t y, uint8_t color);

  // Fill the entire framebuffer with one color (LCD_PIXEL_WHITE / LCD_PIXEL_BLACK).
  void clearBuffer(uint8_t color);

  // Submit a full-frame asynchronous transfer without waiting for SPI; use one caller at low frequency, with no concurrent buffer writes.
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

// ---- Minimal drawing API: buffer-only, no flush; transparent ASCII text requires clearing old backgrounds ----
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
int16_t textWidth(const char *text, uint8_t scale = 1);

// ---- PCF85063 RTC ----
struct RtcDateTime {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t weekday;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

// This example uses Wire exclusively; do not initialize the same controller with IDF i2c_master.
bool boardI2cBegin();
bool rtcBegin();
// true only means the read succeeded; clockInvalid is the OS oscillator-stop flag. The application checks date ranges separately.
// RTC stores local time for 2000..2099, without a time zone or automatic ESP32 system-clock synchronization.
bool rtcRead(RtcDateTime *out, bool *clockInvalid = nullptr);
bool rtcWrite(const RtcDateTime &value);

// ---- SHTC3 indoor temperature/humidity ----
struct Shtc3Sample {
  float temperatureC;
  float humidityPct;
};

// begin wakes, reads ID/checks CRC, then tries to sleep; measure waits synchronously and converts the result.
// Use out only when measure returns true; sleep-command failures are currently ignored.
bool shtc3Begin();
bool shtc3Measure(Shtc3Sample *out);

// ---- KEY (active low, 30 ms debounce): call update continuously; synchronous network calls may miss short presses ----
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
