#include <Arduino.h>
#include "board_pins.h"
#include "shtc3_bsp.h"

bool Shtc3Sensor::begin(I2cBus &bus) {
  return bus.attachDevice(I2C_ADDR_SHTC3, 400000, &dev_);
}

bool Shtc3Sensor::sendCommand(uint16_t cmd) {
  // Send the command in big-endian order: high byte first.
  uint8_t buf[2] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF)};
  return i2cRawWrite(dev_, buf, sizeof(buf));
}

// SHTC3 CRC-8: polynomial 0x31, initial value 0xFF, no input/output reflection, MSB first.
// Reference: https://sensirion.com/file/datasheet_shtc3
uint8_t Shtc3Sensor::crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x31);
      else            crc = (uint8_t)(crc << 1);
    }
  }
  return crc;
}

bool Shtc3Sensor::readId(uint16_t *idOut) {
  if (!sendCommand(0xEFC8)) return false;
  uint8_t buf[3];
  if (!i2cRawRead(dev_, buf, sizeof(buf))) return false;
  if (crc8(buf, 2) != buf[2]) return false;
  *idOut = (uint16_t)((buf[0] << 8) | buf[1]);
  return true;
}

Shtc3Error Shtc3Sensor::measure(Shtc3Sample *out) {
  if (!dev_) return SHTC3_ERR_I2C;

  // 1) Wake up. The datasheet requires about 240 us before the next command;
  //    a 1 ms delay provides sufficient margin here.
  if (!sendCommand(0x3517)) return SHTC3_ERR_I2C;
  delay(1);

  // 2) Start measurement: normal mode, temperature first, no clock stretching.
  if (!sendCommand(0x7866)) return SHTC3_ERR_I2C;

  // 3) Measurement takes about 12 ms; wait 15 ms before reading.
  delay(15);

  uint8_t raw[6];
  if (!i2cRawRead(dev_, raw, sizeof(raw))) return SHTC3_ERR_I2C;

  // 4) After reading the complete result, try to sleep, then validate the cached data on the host.
  //    The sleep command's result is ignored; earlier I2C errors return before reaching this step.
  sendCommand(0xB098);

  // 5) Each data block has its own CRC; check both separately.
  if (crc8(&raw[0], 2) != raw[2]) return SHTC3_ERR_CRC;
  if (crc8(&raw[3], 2) != raw[5]) return SHTC3_ERR_CRC;

  uint16_t rawT = (uint16_t)((raw[0] << 8) | raw[1]);
  uint16_t rawH = (uint16_t)((raw[3] << 8) | raw[4]);

  // Standard conversion formulas from the datasheet.
  out->temperatureC = -45.0f + 175.0f * (float)rawT / 65536.0f;
  out->humidityPct = 100.0f * (float)rawH / 65536.0f;
  return SHTC3_OK;
}
