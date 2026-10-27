#include <string.h>
#include <Arduino.h>
#include "display_bsp.h"

// ============================================================
// ST7305 stores 8 pixels per byte, but not as 8 consecutive horizontal pixels.
// Landscape packs 2 columns x 4 rows; portrait packs 4 columns x 2 rows. See example 03.
//
// This example reuses example 07's lookup tables: (x,y) -> byte index and bit mask.
// At 400x300, the framebuffer uses 15000 bytes and the two tables use another 360000 bytes of PSRAM.
// Lookup avoids repeated mapping calculations, but table/framebuffer accesses still depend on PSRAM latency.
// ============================================================

RlcdPanel::RlcdPanel(int pinMosi, int pinSck, int pinDc, int pinCs, int pinRst,
                     int16_t width, int16_t height, spi_host_device_t spiHost)
    : pinRst_(pinRst),
      width_(width),
      height_(height),
      landscapeMode_(width > height),
      ioHandle_(nullptr),
      frameBuffer_(nullptr),
      frameBytes_(0),
      lutByteIndex_(nullptr),
      lutBitMask_(nullptr) {
  spi_bus_config_t busCfg = {};
  busCfg.mosi_io_num = pinMosi;
  busCfg.sclk_io_num = pinSck;
  busCfg.miso_io_num = -1;
  busCfg.quadwp_io_num = -1;
  busCfg.quadhd_io_num = -1;
  busCfg.max_transfer_sz = width_ * height_ / 8;
  ESP_ERROR_CHECK(spi_bus_initialize(spiHost, &busCfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t ioCfg = {};
  ioCfg.dc_gpio_num = pinDc;
  ioCfg.cs_gpio_num = pinCs;
  ioCfg.pclk_hz = 10 * 1000 * 1000;   // SPI clock must not exceed 24MHz on this board; default is 10MHz
  ioCfg.lcd_cmd_bits = 8;
  ioCfg.lcd_param_bits = 8;
  ioCfg.spi_mode = 0;
  ioCfg.trans_queue_depth = 10;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spiHost,
                                           &ioCfg, &ioHandle_));

  gpio_config_t rstCfg = {};
  rstCfg.mode = GPIO_MODE_OUTPUT;
  rstCfg.pin_bit_mask = (1ULL << pinRst_);
  rstCfg.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&rstCfg));
}

void RlcdPanel::hardReset() {
  // Panel reset: high for 50ms -> low for 20ms -> high for 50ms. Keep all delays.
  gpio_set_level((gpio_num_t)pinRst_, 1);
  delay(50);
  gpio_set_level((gpio_num_t)pinRst_, 0);
  delay(20);
  gpio_set_level((gpio_num_t)pinRst_, 1);
  delay(50);
}

void RlcdPanel::sendCommand(uint8_t cmd) {
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(ioHandle_, cmd, nullptr, 0));
}

void RlcdPanel::sendData(uint8_t data) {
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(ioHandle_, -1, &data, 1));
}

static void deriveWindow(int16_t w, int16_t h,
                         uint8_t *colStart, uint8_t *colEnd,
                         uint8_t *pageStart, uint8_t *pageEnd) {
  const uint16_t kRamColOrigin = 0x12;   // Panel-specific reference value: first visible RAM column
  int16_t sourceLines = (w < h) ? w : h;
  int16_t gateLines = (w > h) ? w : h;
  *colStart = kRamColOrigin;
  *colEnd = kRamColOrigin + sourceLines / 12 - 1;
  *pageStart = 0;
  *pageEnd = gateLines / 2 - 1;
}

bool RlcdPanel::buildLookupTables() {
  size_t pixelCount = (size_t)width_ * height_;
  lutByteIndex_ = (uint16_t *)heap_caps_malloc(pixelCount * sizeof(uint16_t),
                                               MALLOC_CAP_SPIRAM);
  lutBitMask_ = (uint8_t *)heap_caps_malloc(pixelCount,
                                            MALLOC_CAP_SPIRAM);
  if (!lutByteIndex_ || !lutBitMask_) return false;

  for (int16_t y = 0; y < height_; y++) {
    for (int16_t x = 0; x < width_; x++) {
      size_t slot = (size_t)y * width_ + x;
      uint16_t byteIndex;
      uint8_t mask;

      if (landscapeMode_) {
        // Landscape: flip Y, then locate the byte and bit in a 2-column x 4-row block.
        int16_t flipY = height_ - 1 - y;
        byteIndex = (uint16_t)((x >> 1) * (height_ >> 2) + (flipY >> 2));
        uint8_t lane = ((flipY & 3) << 1) | (x & 1);
        mask = (uint8_t)(1u << (7 - lane));
      } else {
        // Portrait: locate the byte and bit in a 4-column x 2-row block.
        byteIndex = (uint16_t)((y >> 1) * (width_ >> 2) + (x >> 2));
        uint8_t lane = ((x & 3) << 1) | (y & 1);
        mask = (uint8_t)(1u << (7 - lane));
      }

      lutByteIndex_[slot] = byteIndex;
      lutBitMask_[slot] = mask;
    }
  }
  return true;
}

bool RlcdPanel::begin() {
  frameBytes_ = width_ * height_ / 8;
  frameBuffer_ = (uint8_t *)heap_caps_malloc(frameBytes_, MALLOC_CAP_SPIRAM);
  if (!frameBuffer_) return false;

  if (!buildLookupTables()) return false;

  deriveWindow(width_, height_, &winColStart_, &winColEnd_,
               &winPageStart_, &winPageEnd_);

  hardReset();
  applyPowerOnInitSequence();

  clearBuffer(LCD_PIXEL_WHITE);
  return true;
}

void RlcdPanel::applyPowerOnInitSequence() {
  // ST7305 power-on register sequence: preserve this panel's reference values and command order.
  sendCommand(0xD6); sendData(0x17); sendData(0x02);  // NVM parameter-loading control
  sendCommand(0xD1); sendData(0x01);                  // Enable boost
  sendCommand(0xC0); sendData(0x11); sendData(0x04);  // Gate voltage settings

  sendCommand(0xC1);                                  // VSHP
  sendData(0x69); sendData(0x69); sendData(0x69); sendData(0x69);
  sendCommand(0xC2);                                  // VSLP
  sendData(0x19); sendData(0x19); sendData(0x19); sendData(0x19);
  sendCommand(0xC4);                                  // VSHN
  sendData(0x4B); sendData(0x4B); sendData(0x4B); sendData(0x4B);
  sendCommand(0xC5);                                  // VSLN
  sendData(0x19); sendData(0x19); sendData(0x19); sendData(0x19);

  sendCommand(0xD8); sendData(0x80); sendData(0xE9);  // Oscillator configuration
  sendCommand(0xB2); sendData(0x02);                  // Frame-rate control

  sendCommand(0xB3);                                  // Update-cycle gating (HPM)
  sendData(0xE5); sendData(0xF6); sendData(0x05); sendData(0x46);
  sendData(0x77); sendData(0x77); sendData(0x77); sendData(0x77);
  sendData(0x76); sendData(0x45);

  sendCommand(0xB4);                                  // Update-cycle gating (LPM)
  sendData(0x05); sendData(0x46); sendData(0x77); sendData(0x77);
  sendData(0x77); sendData(0x77); sendData(0x76); sendData(0x45);

  sendCommand(0x62);                                  // Gate scan timing
  sendData(0x32); sendData(0x03); sendData(0x1F);

  sendCommand(0xB7); sendData(0x13);                  // Enable source EQ
  sendCommand(0xB0); sendData(0x64);                  // Duty: 400-line scanning

  sendCommand(0x11);                                  // SLPOUT: exit sleep
  delay(200);

  sendCommand(0xC9); sendData(0x00);                  // Source voltage selection
  sendCommand(0x36); sendData(0x48);                  // MADCTL: scan direction
  sendCommand(0x3A); sendData(0x11);                  // Data format: 1bpp
  sendCommand(0xB9); sendData(0x20);                  // Grayscale mode
  sendCommand(0xB8); sendData(0x29);                  // Panel settings

  sendCommand(0x20);                                  // Display inversion off (0 = white, 1 = black)

  sendCommand(0x2A); sendData(winColStart_); sendData(winColEnd_);
  sendCommand(0x2B); sendData(winPageStart_); sendData(winPageEnd_);

  sendCommand(0x35); sendData(0x00);                  // TE settings; this program does not use TE synchronization
  sendCommand(0xD0); sendData(0xFF);                  // Enable automatic power-off
  sendCommand(0x38);                                  // Enable high-power mode
  sendCommand(0x29);                                  // Display on
}

void RlcdPanel::clearBuffer(uint8_t color) {
  memset(frameBuffer_, color ? 0xFF : 0x00, frameBytes_);
}

// Calculate the mapping directly; the resulting framebuffer layout matches the lookup method.
void RlcdPanel::setPixelSlow(int16_t x, int16_t y, uint8_t color) {
  if (!frameBuffer_) return;
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;

  bool black = (color != LCD_PIXEL_WHITE);
  uint16_t byteIndex;
  uint8_t lane;

  if (landscapeMode_) {
    int16_t flipY = height_ - 1 - y;
    byteIndex = (uint16_t)((x >> 1) * (height_ >> 2) + (flipY >> 2));
    lane = ((flipY & 3) << 1) | (x & 1);
  } else {
    byteIndex = (uint16_t)((y >> 1) * (width_ >> 2) + (x >> 2));
    lane = ((x & 3) << 1) | (y & 1);
  }
  uint8_t mask = (uint8_t)(1u << (7 - lane));

  if (black) frameBuffer_[byteIndex] |= mask;
  else       frameBuffer_[byteIndex] &= ~mask;
}

// Read both tables, then perform a read-modify-write on the target byte.
void RlcdPanel::setPixel(int16_t x, int16_t y, uint8_t color) {
  if (!frameBuffer_) return;
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;

  size_t slot = (size_t)y * width_ + x;
  uint16_t byteIndex = lutByteIndex_[slot];
  uint8_t mask = lutBitMask_[slot];

  if (color != LCD_PIXEL_WHITE) frameBuffer_[byteIndex] |= mask;
  else                          frameBuffer_[byteIndex] &= ~mask;
}

void RlcdPanel::flush() {
  sendCommand(0x2A);
  sendData(winColStart_);
  sendData(winColEnd_);
  sendCommand(0x2B);
  sendData(winPageStart_);
  sendData(winPageEnd_);
  sendCommand(0x2C);
  // Data may still be in transit when this API returns; SPI continues to reference frameBuffer_.
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(ioHandle_, -1,
                                            frameBuffer_, frameBytes_));
}
