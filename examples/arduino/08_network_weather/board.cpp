#include <string.h>
#include <Arduino.h>
#include <Wire.h>
#include "board.h"
#include "font8x8.h"

// ============================================================
// ST7305 pixel packing (the main topic of this chapter)
//
// 1) Why are 8 horizontal pixels not stored in one byte?
//    This is a 1bpp (1 bit per pixel) panel, but its RAM does not store 8
//    horizontal pixels per byte. Instead, pixels are arranged in small blocks:
//    one [4 pixels wide x 2 rows high] block per byte in portrait, or [2 columns x 4 rows] in landscape.
//    The ST7305 source/gate scan structure determines this layout, which the driver must follow.
//
// 2) Portrait (300x400) packing -- each byte = one 4x2 block:
//      cellX    = x / 4                    (groups of 4 horizontal pixels)
//      rowPair  = y / 2                    (pairs of vertical rows)
//      byteIndex = rowPair * (width/4) + cellX
//      In-block offsets: lx = x%4, ly = y%2
//      bitIndex = 7 - (lx*2 + ly)          (most significant bit first)
//
// 3) Why is the landscape (400x300) formula different?
//      Landscape rotates the native portrait panel by 90 degrees, but does not rotate its RAM layout.
//      Writes therefore advance column-first, and the y direction must be flipped vertically:
//      flipY    = (height-1) - y           (landscape y runs opposite to RAM rows)
//      colCell  = x / 2                    (groups of 2 horizontal pixels)
//      rowBlock = flipY / 4                (groups of 4 vertical rows)
//      byteIndex = colCell * (height/4) + rowBlock
//      bitIndex = 7 - ((flipY%4)*2 + x%2)
//
//    Each orientation uses its own coordinate mapping: locate the byte, then the pixel bit within it.
// ============================================================

RlcdPanel::RlcdPanel(int pinMosi, int pinSck, int pinDc, int pinCs, int pinRst,
                     int16_t width, int16_t height, spi_host_device_t spiHost)
    : pinDc_(pinDc),
      pinCs_(pinCs),
      pinRst_(pinRst),
      width_(width),
      height_(height),
      landscapeMode_(width > height),
      ioHandle_(nullptr),
      frameBuffer_(nullptr),
      frameBytes_(0) {
  // --- SPI bus ---
  // max_transfer_sz must accommodate a full-frame DMA transfer.
  spi_bus_config_t busCfg = {};
  busCfg.mosi_io_num = pinMosi;
  busCfg.sclk_io_num = pinSck;
  busCfg.miso_io_num = -1;
  busCfg.quadwp_io_num = -1;
  busCfg.quadhd_io_num = -1;
  busCfg.max_transfer_sz = width_ * height_ / 8;
  ESP_ERROR_CHECK(spi_bus_initialize(spiHost, &busCfg, SPI_DMA_CH_AUTO));

  // --- Panel IO (esp_lcd manages DC/CS) ---
  // Keep this board's SPI clock at or below 24 MHz; the course uses 10 MHz throughout.
  esp_lcd_panel_io_spi_config_t ioCfg = {};
  ioCfg.dc_gpio_num = pinDc_;
  ioCfg.cs_gpio_num = pinCs_;
  ioCfg.pclk_hz = 10 * 1000 * 1000;
  ioCfg.lcd_cmd_bits = 8;
  ioCfg.lcd_param_bits = 8;
  ioCfg.spi_mode = 0;
  ioCfg.trans_queue_depth = 10;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spiHost,
                                           &ioCfg, &ioHandle_));

  // Control RST manually (see hardReset for the reset sequence).
  gpio_config_t rstCfg = {};
  rstCfg.mode = GPIO_MODE_OUTPUT;
  rstCfg.pin_bit_mask = (1ULL << pinRst_);
  rstCfg.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&rstCfg));
}

void RlcdPanel::hardReset() {
  // Standard hardware reset sequence: high (settle) -> low (reset) -> high (ready).
  // Panel reset delays: 50 ms / 20 ms / 50 ms, preserving settling time at each stage.
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

// Address window: derived once from the resolution in begin().
// Result for this panel: columns 0x12..0x2A, pages 0x00..0xC7.
//   Page address = [0, gateLines/2 - 1]                  400 gate lines -> 200 pages
//   Column address = [ramColOrigin, ramColOrigin + sourceLines/12 - 1]  300 source lines -> 25 column units
// RAM column origin 0x12 is specific to this panel's reference settings, not universal to all ST7305 panels.
// Derive the window from this panel's source/gate counts; both orientations use the same visible RAM region.
static void deriveWindow(int16_t w, int16_t h,
                         uint8_t *colStart, uint8_t *colEnd,
                         uint8_t *pageStart, uint8_t *pageEnd) {
  const uint16_t kRamColOrigin = 0x12;   // First visible ST7305 RAM column
  int16_t sourceLines = (w < h) ? w : h; // Number of source lines on the native panel
  int16_t gateLines = (w > h) ? w : h;   // Number of gate lines on the native panel

  *colStart = kRamColOrigin;
  *colEnd = kRamColOrigin + sourceLines / 12 - 1;
  *pageStart = 0;
  *pageEnd = gateLines / 2 - 1;
}

bool RlcdPanel::begin() {
  // Keep the framebuffer in SPIRAM: 15000 bytes is modest, but external RAM
  // leaves precious internal heap for speed-sensitive uses such as LVGL/audio buffers.
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

  clearBuffer(LCD_PIXEL_WHITE);   // Clear memory only; the caller still flushes the first frame.
  return true;
}

void RlcdPanel::applyPowerOnInitSequence() {
  // ------------------------------------------------------------
  // ST7305 power-on register sequence.
  // This panel uses settings from the reference example; see the comments for command meanings.
  // Preserve the values and order together; this is not a universal configuration for all ST7305 panels.
  // ------------------------------------------------------------

  sendCommand(0xD6); sendData(0x17); sendData(0x02);  // NVM parameter loading control
  sendCommand(0xD1); sendData(0x01);                  // Enable the boost circuit
  sendCommand(0xC0); sendData(0x11); sendData(0x04);  // Set gate voltages VGH/VGL

  // Retain this panel's drive-voltage settings; these are not ordinary UI configuration options.
  sendCommand(0xC1);                                  // VSHP
  sendData(0x69); sendData(0x69); sendData(0x69); sendData(0x69);
  sendCommand(0xC2);                                  // VSLP
  sendData(0x19); sendData(0x19); sendData(0x19); sendData(0x19);
  sendCommand(0xC4);                                  // VSHN
  sendData(0x4B); sendData(0x4B); sendData(0x4B); sendData(0x4B);
  sendCommand(0xC5);                                  // VSLN
  sendData(0x19); sendData(0x19); sendData(0x19); sendData(0x19);

  sendCommand(0xD8); sendData(0x80); sendData(0xE9);  // Internal oscillator configuration
  sendCommand(0xB2); sendData(0x02);                  // Frame rate control

  sendCommand(0xB3);                                  // High-power mode update-cycle gating (HPM)
  sendData(0xE5); sendData(0xF6); sendData(0x05); sendData(0x46);
  sendData(0x77); sendData(0x77); sendData(0x77); sendData(0x77);
  sendData(0x76); sendData(0x45);

  sendCommand(0xB4);                                  // Low-power mode update-cycle gating (LPM)
  sendData(0x05); sendData(0x46); sendData(0x77); sendData(0x77);
  sendData(0x77); sendData(0x77); sendData(0x76); sendData(0x45);

  sendCommand(0x62);                                  // Gate scan timing
  sendData(0x32); sendData(0x03); sendData(0x1F);

  sendCommand(0xB7); sendData(0x13);                  // Enable Source EQ
  sendCommand(0xB0); sendData(0x64);                  // Duty cycle: 400-line scan

  sendCommand(0x11);                                  // SLPOUT: exit sleep
  delay(200);                                         // Wait for internal power to stabilize

  sendCommand(0xC9); sendData(0x00);                  // Source voltage selection
  sendCommand(0x36); sendData(0x48);                  // Scan settings must match the setPixel() coordinate mapping.
  sendCommand(0x3A); sendData(0x11);                  // Data format: 1bpp packed mode
  sendCommand(0xB9); sendData(0x20);                  // Grayscale mode
  sendCommand(0xB8); sendData(0x29);                  // Panel settings (inversion/interlacing)

  sendCommand(0x20);                                  // Disable display inversion (0=white, 1=black)

  // Set the default full-screen window once (flush sets it again); establish a known power-on state.
  sendCommand(0x2A); sendData(winColStart_); sendData(winColEnd_);
  sendCommand(0x2B); sendData(winPageStart_); sendData(winPageEnd_);

  sendCommand(0x35); sendData(0x00);                  // Retain TE configuration; the program does not use TE synchronization.
  sendCommand(0xD0); sendData(0xFF);                  // Enable automatic power-down
  sendCommand(0x38);                                  // High-power mode (faster refresh)
  sendCommand(0x29);                                  // DISPLAY ON
}

void RlcdPanel::clearBuffer(uint8_t color) {
  memset(frameBuffer_, color ? 0xFF : 0x00, frameBytes_);
}

// ---- Portrait packing: each byte covers [4 wide x 2 high] ----
static void writePortraitPixel(uint8_t *fb, int16_t w,
                               int16_t x, int16_t y, bool black) {
  int16_t cellX = x >> 2;              // x/4: horizontal group of 4 pixels
  int16_t rowPair = y >> 1;            // y/2: vertical row pair
  uint16_t byteIndex = rowPair * (w >> 2) + cellX;

  uint8_t laneInCell = ((x & 3) << 1) | (y & 1);  // Slot 0-7 within the block
  uint8_t mask = 1 << (7 - laneInCell);           // Most significant bit first

  if (black) fb[byteIndex] |= mask;
  else       fb[byteIndex] &= ~mask;
}

// ---- Landscape packing: each byte covers [2 wide x 4 high], with y flipped vertically ----
static void writeLandscapePixel(uint8_t *fb, int16_t w, int16_t h,
                                int16_t x, int16_t y, bool black) {
  int16_t flipY = h - 1 - y;           // Landscape y runs opposite to RAM rows.
  int16_t colCell = x >> 1;            // x/2: horizontal group of 2 pixels
  int16_t rowBlock = flipY >> 2;       // flipped y/4: vertical block of 4 rows
  uint16_t byteIndex = colCell * (h >> 2) + rowBlock;

  uint8_t laneInCell = ((flipY & 3) << 1) | (x & 1);
  uint8_t mask = 1 << (7 - laneInCell);

  if (black) fb[byteIndex] |= mask;
  else       fb[byteIndex] &= ~mask;
}

void RlcdPanel::setPixel(int16_t x, int16_t y, uint8_t color) {
  if (!frameBuffer_) return;
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;  // Silently ignore out-of-bounds coordinates.

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
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(ioHandle_, -1,
                                            frameBuffer_, frameBytes_));
}

void RlcdPanel::flush() {
  // tx_color only submits the transfer; returning does not mean SPI transmission has completed.
  pushFrameWindow();
}

// ============================================================
// Minimal drawing helpers.
// ============================================================

void drawLine(RlcdPanel &panel, int16_t x0, int16_t y0,
              int16_t x1, int16_t y1, uint8_t color) {
  int16_t dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
  int16_t dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
  int16_t err = dx + dy;

  while (true) {
    panel.setPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = err * 2;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void drawRect(RlcdPanel &panel, int16_t x, int16_t y,
              int16_t w, int16_t h, uint8_t color) {
  drawLine(panel, x, y, x + w - 1, y, color);
  drawLine(panel, x, y + h - 1, x + w - 1, y + h - 1, color);
  drawLine(panel, x, y, x, y + h - 1, color);
  drawLine(panel, x + w - 1, y, x + w - 1, y + h - 1, color);
}

void fillRect(RlcdPanel &panel, int16_t x, int16_t y,
              int16_t w, int16_t h, uint8_t color) {
  for (int16_t row = 0; row < h; row++) {
    for (int16_t col = 0; col < w; col++) {
      panel.setPixel(x + col, y + row, color);
    }
  }
}

void drawChar(RlcdPanel &panel, int16_t x, int16_t y,
              char ch, uint8_t color, uint8_t scale) {
  if (ch < FONT8X8_FIRST_CHAR || ch > FONT8X8_LAST_CHAR) ch = '?';
  const unsigned char *glyph = kFont8x8[ch - FONT8X8_FIRST_CHAR];

  for (uint8_t row = 0; row < 8; row++) {
    for (uint8_t col = 0; col < 8; col++) {
      if (glyph[row] & (1U << col)) {
        fillRect(panel, x + col * scale, y + row * scale,
                 scale, scale, color);
      }
    }
  }
}

void drawString(RlcdPanel &panel, int16_t x, int16_t y,
                const char *text, uint8_t color, uint8_t scale) {
  while (*text) {
    drawChar(panel, x, y, *text++, color, scale);
    x += 8 * scale;
  }
}

int16_t textWidth(const char *text, uint8_t scale) {
  return (int16_t)(strlen(text) * 8 * scale);
}

// ============================================================
// Arduino Wire：PCF85063 + SHTC3
// ============================================================

namespace {
constexpr uint8_t kRtcAddress = 0x51;
constexpr uint8_t kShtc3Address = 0x70;

bool i2cWrite(uint8_t address, const uint8_t *data, size_t length) {
  Wire.beginTransmission(address);
  Wire.write(data, length);
  return Wire.endTransmission() == 0;
}

bool i2cWriteRegister(uint8_t address, uint8_t reg,
                      const uint8_t *data, size_t length) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(data, length);
  return Wire.endTransmission() == 0;
}

bool i2cReadRegister(uint8_t address, uint8_t reg,
                     uint8_t *data, size_t length) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  // false retains the bus for a repeated START read, rather than two separate transactions.
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, length) != length) return false;
  for (size_t i = 0; i < length; i++) data[i] = (uint8_t)Wire.read();
  return true;
}

bool i2cRead(uint8_t address, uint8_t *data, size_t length) {
  if (Wire.requestFrom(address, length) != length) return false;
  for (size_t i = 0; i < length; i++) data[i] = (uint8_t)Wire.read();
  return true;
}

uint8_t fromBcd(uint8_t value) {
  return (uint8_t)((value >> 4) * 10 + (value & 0x0F));
}

uint8_t toBcd(uint8_t value) {
  return (uint8_t)(((value / 10) << 4) | (value % 10));
}

bool shtc3Command(uint16_t command) {
  uint8_t data[2] = {(uint8_t)(command >> 8), (uint8_t)command};
  return i2cWrite(kShtc3Address, data, sizeof(data));
}

uint8_t shtc3Crc(const uint8_t *data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31)
                         : (uint8_t)(crc << 1);
    }
  }
  return crc;
}
}  // namespace

bool boardI2cBegin() {
  Wire.setTimeOut(200);
  return Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
}

bool rtcBegin() {
  uint8_t control = 0;
  if (!i2cReadRegister(kRtcAddress, 0x00, &control, 1)) return false;
  control &= (uint8_t)~((1U << 5) | (1U << 1));  // Running, 24-hour mode.
  return i2cWriteRegister(kRtcAddress, 0x00, &control, 1);
}

bool rtcRead(RtcDateTime *out, bool *clockInvalid) {
  if (!out) return false;
  uint8_t raw[7];
  if (!i2cReadRegister(kRtcAddress, 0x04, raw, sizeof(raw))) return false;

  // OS shares the seconds register and must be interpreted separately; reads do not clear OS.
  if (clockInvalid) *clockInvalid = (raw[0] & 0x80) != 0;
  out->second = fromBcd(raw[0] & 0x7F);
  out->minute = fromBcd(raw[1] & 0x7F);
  out->hour = fromBcd(raw[2] & 0x3F);
  out->day = fromBcd(raw[3] & 0x3F);
  out->weekday = raw[4] & 0x07;
  out->month = fromBcd(raw[5] & 0x1F);
  out->year = 2000 + fromBcd(raw[6]);
  return true;
}

bool rtcWrite(const RtcDateTime &value) {
  if (value.year < 2000 || value.year > 2099) return false;
  uint8_t raw[7] = {
      (uint8_t)(toBcd(value.second) & 0x7F),
      toBcd(value.minute),
      toBcd(value.hour),
      toBcd(value.day),
      (uint8_t)(value.weekday % 7),
      toBcd(value.month),
      toBcd((uint8_t)(value.year - 2000)),
  };
  return i2cWriteRegister(kRtcAddress, 0x04, raw, sizeof(raw));
}

bool shtc3Begin() {
  if (!shtc3Command(0x3517)) return false;  // wake
  delay(1);
  if (!shtc3Command(0xEFC8)) {              // read ID
    shtc3Command(0xB098);
    return false;
  }
  delay(1);
  uint8_t id[3];
  const bool ok = i2cRead(kShtc3Address, id, sizeof(id)) &&
                  shtc3Crc(id, 2) == id[2];
  shtc3Command(0xB098);  // sleep
  return ok;
}

bool shtc3Measure(Shtc3Sample *out) {
  if (!out || !shtc3Command(0x3517)) return false;
  delay(1);
  if (!shtc3Command(0x7866)) {
    shtc3Command(0xB098);
    return false;
  }
  delay(15);

  uint8_t raw[6];
  // Read both temperature/CRC and humidity/CRC blocks; validate both before writing out.
  const bool readOk = i2cRead(kShtc3Address, raw, sizeof(raw));
  shtc3Command(0xB098);
  if (!readOk || shtc3Crc(raw, 2) != raw[2] ||
      shtc3Crc(raw + 3, 2) != raw[5]) {
    return false;
  }

  const uint16_t rawTemperature = (uint16_t)((raw[0] << 8) | raw[1]);
  const uint16_t rawHumidity = (uint16_t)((raw[3] << 8) | raw[4]);
  out->temperatureC = -45.0f + 175.0f * rawTemperature / 65536.0f;
  out->humidityPct = 100.0f * rawHumidity / 65536.0f;
  return true;
}

DebouncedButton::DebouncedButton(uint8_t pin) : pin_(pin) {}

void DebouncedButton::begin() {
  pinMode(pin_, INPUT_PULLUP);
  rawDown_ = digitalRead(pin_) == LOW;
  stableDown_ = rawDown_;
  changedAtMs_ = millis();
}

void DebouncedButton::update() {
  const bool down = digitalRead(pin_) == LOW;
  const uint32_t now = millis();
  if (down != rawDown_) {
    rawDown_ = down;
    changedAtMs_ = now;
  } else if (down != stableDown_ && now - changedAtMs_ >= 30) {
    stableDown_ = down;
    if (stableDown_) pressPending_ = true;
  }
}

bool DebouncedButton::consumePress() {
  const bool pressed = pressPending_;
  pressPending_ = false;
  return pressed;
}
