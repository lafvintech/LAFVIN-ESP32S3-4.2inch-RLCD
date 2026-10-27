#include <string.h>
#include <Arduino.h>
#include "display_bsp.h"

// ============================================================
// ST7305 pixel packing (the main teaching focus of this example)
//
// 1) Why are 4 pixels packed into 1 byte?
//    This is a 1bpp (1 bit per pixel) panel, but its RAM does not store 8 consecutive
//    horizontal pixels per byte. Pixels are arranged in blocks: in portrait, one byte
//    covers [4 pixels wide x 2 rows high]; in landscape, [2 columns wide x 4 rows high].
//    The ST7305 panel's source/gate scanning structure determines this layout, so the driver must follow it.
//
// 2) Portrait (300x400) packing -- each byte covers a 4x2 block:
//      cellX     = x / 4                    (groups of 4 horizontal pixels)
//      rowPair   = y / 2                    (groups of 2 vertical rows)
//      byteIndex = rowPair * (width/4) + cellX
//      Offsets within the block: lx = x%4, ly = y%2
//      bit       = 7 - (lx*2 + ly)          (MSB first)
//
// 3) Why does landscape (400x300) use a different formula?
//      Landscape rotates the native portrait panel by 90 degrees; the RAM does not rotate,
//      so writes become column-major and the Y direction must be flipped:
//      flipY     = (height-1) - y           (landscape Y runs opposite to RAM rows)
//      colCell   = x / 2                    (groups of 2 horizontal pixels)
//      rowBlock  = flipY / 4                (groups of 4 vertical rows)
//      byteIndex = colCell * (height/4) + rowBlock
//      bit       = 7 - ((flipY%4)*2 + x%2)
//
//    Each orientation uses its own coordinate mapping: locate the byte, then the pixel bit within it.
// ============================================================

RlcdPanel::RlcdPanel(int pinMosi, int pinSck, int pinDc, int pinCs, int pinRst,
                     int16_t width, int16_t height, spi_host_device_t spiHost)
    : pinDc_(pinDc), pinCs_(pinCs), pinRst_(pinRst), width_(width), height_(height),
      landscapeMode_(width > height), ioHandle_(nullptr), frameBuffer_(nullptr), frameBytes_(0),
      pinMosi_(pinMosi), pinSck_(pinSck), spiHost_(spiHost) {}

bool RlcdPanel::colorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *context) {
  RlcdPanel *self = static_cast<RlcdPanel *>(context);
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(self->transferDone_, &woken);
  return woken == pdTRUE;
}

bool RlcdPanel::initIo() {
  transferDone_ = xSemaphoreCreateBinary();
  if (!transferDone_) return false;
  spi_bus_config_t cfg = {};
  cfg.mosi_io_num = pinMosi_; cfg.sclk_io_num = pinSck_; cfg.miso_io_num = -1;
  cfg.quadwp_io_num = -1; cfg.quadhd_io_num = -1;
  cfg.max_transfer_sz = width_ * height_ / 8;
  if (spi_bus_initialize(spiHost_, &cfg, SPI_DMA_CH_AUTO) != ESP_OK) return false;
  esp_lcd_panel_io_spi_config_t io = {};
  io.dc_gpio_num = pinDc_; io.cs_gpio_num = pinCs_; io.pclk_hz = 10 * 1000 * 1000;
  io.lcd_cmd_bits = 8; io.lcd_param_bits = 8; io.spi_mode = 0; io.trans_queue_depth = 1;
  io.on_color_trans_done = colorDone; io.user_ctx = this;
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spiHost_, &io, &ioHandle_) != ESP_OK) return false;
  gpio_config_t rst = {};
  rst.mode = GPIO_MODE_OUTPUT; rst.pin_bit_mask = 1ULL << pinRst_; rst.pull_up_en = GPIO_PULLUP_ENABLE;
  return gpio_config(&rst) == ESP_OK;
}

void RlcdPanel::hardReset() {
  // Standard hardware reset sequence: high (stabilize) -> low (reset) -> high (ready)
  // Panel reset delays: 50ms / 20ms / 50ms. Preserve each stabilization interval.
  gpio_set_level((gpio_num_t)pinRst_, 1);
  delay(50);
  gpio_set_level((gpio_num_t)pinRst_, 0);
  delay(20);
  gpio_set_level((gpio_num_t)pinRst_, 1);
  delay(50);
}

void RlcdPanel::sendCommand(uint8_t cmd) {
  if (!ioError_ && esp_lcd_panel_io_tx_param(ioHandle_, cmd, nullptr, 0) != ESP_OK) ioError_ = true;
}

void RlcdPanel::sendData(uint8_t data) {
  if (!ioError_ && esp_lcd_panel_io_tx_param(ioHandle_, -1, &data, 1) != ESP_OK) ioError_ = true;
}

// Address window: derive it once from the resolution in begin().
// For this panel: columns 0x12..0x2A, pages 0x00..0xC7.
//   Page range   = [0, gateLines/2 - 1]                    400 gates -> 200 pages
//   Column range = [ramColOrigin, ramColOrigin + sourceLines/12 - 1]  300 sources -> 25 column units
// RAM column origin 0x12 is a reference value for this panel, not a universal ST7305 setting.
// Derive the window from this panel's source/gate counts; landscape and portrait share the visible RAM range.
static void deriveWindow(int16_t w, int16_t h,
                         uint8_t *colStart, uint8_t *colEnd,
                         uint8_t *pageStart, uint8_t *pageEnd) {
  const uint16_t kRamColOrigin = 0x12;   // First visible ST7305 RAM column
  int16_t sourceLines = (w < h) ? w : h; // Native panel source-line count
  int16_t gateLines = (w > h) ? w : h;   // Native panel gate-line count

  *colStart = kRamColOrigin;
  *colEnd = kRamColOrigin + sourceLines / 12 - 1;
  *pageStart = 0;
  *pageEnd = gateLines / 2 - 1;
}

bool RlcdPanel::begin() {
  if (!initIo()) { ioError_ = true; return false; }
  // Allocate the framebuffer in SPIRAM: although 15000 bytes is small, use external RAM
  // to reserve valuable internal heap for LVGL/audio buffers and other speed-sensitive work.
  frameBytes_ = width_ * height_ / 8;
  frameBuffer_ = (uint8_t *)heap_caps_malloc(frameBytes_, MALLOC_CAP_SPIRAM);
  if (frameBuffer_ == nullptr) {
    Serial.println("[panel] ERROR: frame buffer alloc failed");
    return false;
  }

  deriveWindow(width_, height_, &winColStart_, &winColEnd_,
               &winPageStart_, &winPageEnd_);

  hardReset();
  applyPowerOnInitSequence();

  if (ioError_) return false;
  clearBuffer(LCD_PIXEL_WHITE);   // Start with a clean white framebuffer
  return true;
}

void RlcdPanel::applyPowerOnInitSequence() {
  // ------------------------------------------------------------
  // ST7305 power-on register sequence.
  // This panel uses the reference example's register values; command meanings are noted below.
  // Preserve both values and order; these are not universal settings for all ST7305 panels.
  // ------------------------------------------------------------

  sendCommand(0xD6); sendData(0x17); sendData(0x02);  // NVM parameter-loading control
  sendCommand(0xD1); sendData(0x01);                  // Enable the boost circuit
  sendCommand(0xC0); sendData(0x11); sendData(0x04);  // VGH/VGL gate voltage settings

  // These four groups set drive voltages; each has four parameters for four temperature-compensation ranges
  sendCommand(0xC1);                                  // VSHP (positive high voltage)
  sendData(0x69); sendData(0x69); sendData(0x69); sendData(0x69);
  sendCommand(0xC2);                                  // VSLP (negative high voltage)
  sendData(0x19); sendData(0x19); sendData(0x19); sendData(0x19);
  sendCommand(0xC4);                                  // VSHN (positive low voltage)
  sendData(0x4B); sendData(0x4B); sendData(0x4B); sendData(0x4B);
  sendCommand(0xC5);                                  // VSLN (negative low voltage)
  sendData(0x19); sendData(0x19); sendData(0x19); sendData(0x19);

  sendCommand(0xD8); sendData(0x80); sendData(0xE9);  // Internal oscillator configuration
  sendCommand(0xB2); sendData(0x02);                  // Frame-rate control

  sendCommand(0xB3);                                  // High-power-mode update-cycle gating (HPM)
  sendData(0xE5); sendData(0xF6); sendData(0x05); sendData(0x46);
  sendData(0x77); sendData(0x77); sendData(0x77); sendData(0x77);
  sendData(0x76); sendData(0x45);

  sendCommand(0xB4);                                  // Low-power-mode update-cycle gating (LPM)
  sendData(0x05); sendData(0x46); sendData(0x77); sendData(0x77);
  sendData(0x77); sendData(0x77); sendData(0x76); sendData(0x45);

  sendCommand(0x62);                                  // Gate scan timing
  sendData(0x32); sendData(0x03); sendData(0x1F);

  sendCommand(0xB7); sendData(0x13);                  // Enable source EQ
  sendCommand(0xB0); sendData(0x64);                  // Duty: scan 400 lines

  sendCommand(0x11);                                  // SLPOUT: exit sleep
  delay(200);                                         // Wait for internal power supplies to stabilize

  sendCommand(0xC9); sendData(0x00);                  // Source voltage selection
  sendCommand(0x36); sendData(0x48);                  // MADCTL: scan direction/mirroring (determines landscape/portrait mapping)
  sendCommand(0x3A); sendData(0x11);                  // Data format: packed 1bpp
  sendCommand(0xB9); sendData(0x20);                  // Grayscale mode
  sendCommand(0xB8); sendData(0x29);                  // Panel settings (inversion/interlacing)

  sendCommand(0x20);                                  // Display inversion off (0 = white, 1 = black)

  // Set a default full-screen window (flush resets it; this establishes a known power-on state)
  sendCommand(0x2A); sendData(winColStart_); sendData(winColEnd_);
  sendCommand(0x2B); sendData(winPageStart_); sendData(winPageEnd_);

  sendCommand(0x35); sendData(0x00);                  // TE output settings (not connected on this board)
  sendCommand(0xD0); sendData(0xFF);                  // Enable automatic power-off
  sendCommand(0x38);                                  // High-power mode (faster refresh)
  sendCommand(0x29);                                  // DISPLAY ON
}

void RlcdPanel::clearBuffer(uint8_t color) {
  memset(frameBuffer_, color ? 0xFF : 0x00, frameBytes_);
}

// ---- Portrait packing: each byte covers [4 wide x 2 high] ----
static void writePortraitPixel(uint8_t *fb, int16_t w,
                               int16_t x, int16_t y, bool black) {
  int16_t cellX = x >> 2;              // x/4: horizontal 4-pixel group index
  int16_t rowPair = y >> 1;            // y/2: vertical row-pair index
  uint16_t byteIndex = rowPair * (w >> 2) + cellX;

  uint8_t laneInCell = ((x & 3) << 1) | (y & 1);  // Slot 0..7 within the block
  uint8_t mask = 1 << (7 - laneInCell);           // MSB first

  if (black) fb[byteIndex] |= mask;
  else       fb[byteIndex] &= ~mask;
}

// ---- Landscape packing: each byte covers [2 wide x 4 high], with Y flipped ----
static void writeLandscapePixel(uint8_t *fb, int16_t w, int16_t h,
                                int16_t x, int16_t y, bool black) {
  int16_t flipY = h - 1 - y;           // Landscape Y runs opposite to RAM rows
  int16_t colCell = x >> 1;            // x/2: horizontal 2-pixel group index
  int16_t rowBlock = flipY >> 2;       // flipY/4: vertical 4-row block index
  uint16_t byteIndex = colCell * (h >> 2) + rowBlock;

  uint8_t laneInCell = ((flipY & 3) << 1) | (x & 1);
  uint8_t mask = 1 << (7 - laneInCell);

  if (black) fb[byteIndex] |= mask;
  else       fb[byteIndex] &= ~mask;
}

void RlcdPanel::setPixel(int16_t x, int16_t y, uint8_t color) {
  if (!frameBuffer_) return;
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;  // Silently ignore out-of-bounds coordinates

  bool black = (color != LCD_PIXEL_WHITE);
  if (landscapeMode_) {
    writeLandscapePixel(frameBuffer_, width_, height_, x, y, black);
  } else {
    writePortraitPixel(frameBuffer_, width_, x, y, black);
  }
}

void RlcdPanel::pushFrameWindow() {
  sendCommand(0x2A);  // Column address window
  sendData(winColStart_);
  sendData(winColEnd_);
  sendCommand(0x2B);  // Page (row) address window
  sendData(winPageStart_);
  sendData(winPageEnd_);
  sendCommand(0x2C);  // Start writing display RAM
  if (!ioError_ && esp_lcd_panel_io_tx_color(ioHandle_, -1, frameBuffer_, frameBytes_) != ESP_OK)
    ioError_ = true;
}

bool RlcdPanel::flush() {
  if (ioError_ || !frameBuffer_) return false;
  pushFrameWindow();
  if (ioError_ || xSemaphoreTake(transferDone_, pdMS_TO_TICKS(500)) != pdTRUE) {
    ioError_ = true;
    return false;
  }
  return true; // DMA is finished before the caller edits this buffer again.
}
