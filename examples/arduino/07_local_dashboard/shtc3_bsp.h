#pragma once

#include <stdint.h>
#include "i2c_bus_bsp.h"

// ============================================================
// shtc3_bsp -- SHTC3 temperature and humidity sensor driver
//
// Unlike conventional register-address devices, this sensor accepts 16-bit command codes.
// Common command codes (SHTC3 datasheet):
//   0x3517  Wakeup    Wake from sleep
//   0x7866  Measure   Measure temperature first, then humidity, without clock stretching
//   0xB098  Sleep     Enter low-power sleep
//   0xEFC8  ReadID    Read the chip ID
//   0x805D  SoftReset Software reset
//
// The measurement result contains 6 bytes:
//   [temp MSB][temp LSB][temp CRC] [humidity MSB][humidity LSB][humidity CRC]
// CRC-8 uses polynomial 0x31 and initial value 0xFF to detect I2C transmission errors.
// Do not skip CRC validation and use unchecked data in a production device.
// ============================================================

enum Shtc3Error : uint8_t {
  SHTC3_OK = 0,
  SHTC3_ERR_I2C = 1,      // I2C transfer failed.
  SHTC3_ERR_CRC = 2,      // Data CRC validation failed.
};

struct Shtc3Sample {
  float temperatureC;
  float humidityPct;
};

class Shtc3Sensor {
 public:
  // Register a device handle without probing for an ACK; I2cBus owns the bus and retains resources until restart.
  bool begin(I2cBus &bus);

  // Synchronous flow: wake -> wait for measurement -> read -> try to sleep -> validate CRC/convert.
  // out must be valid and is usable only on SHTC3_OK. The sleep command's result is not checked,
  // and intermediate I2C errors return early, so valid data does not confirm that the sensor is asleep.
  Shtc3Error measure(Shtc3Sample *out);

  // Read the 16-bit ID and check CRC; no wake/sleep handling or product-code comparison.
  bool readId(uint16_t *idOut);

 private:
  static uint8_t crc8(const uint8_t *data, size_t len);
  bool sendCommand(uint16_t cmd);

  i2c_master_dev_handle_t dev_ = nullptr;
};
