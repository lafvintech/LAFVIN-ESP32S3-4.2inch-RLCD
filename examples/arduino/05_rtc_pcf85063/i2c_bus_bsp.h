#pragma once

#include <stdint.h>
#include <driver/i2c_master.h>

// ============================================================
// i2c_bus_bsp -- Course-standard I2C bus wrapper (based on the ESP-IDF i2c_master API)
//
// The Arduino version also uses ESP-IDF i2c_master so later sensors/audio can share
// one bus handle, without reinitializing the same I2C controller through Wire.
// I2cBus owns the bus; device drivers hold only their own handles. Resources remain until reboot in this example.
// ============================================================

class I2cBus {
 public:
  // Open the master bus. See board_pins.h for sda/scl. busHz is retained but has no effect;
  // attachDevice() sets each device's actual speed via devHz.
  bool begin(int pinSda, int pinScl, int port = 0, uint32_t busHz = 400000);

  // devAddr is a 7-bit address, e.g. 0x51; do not shift it or append the read/write bit.
  // This only registers the device, without probing for an ACK; the first actual transfer checks availability.
  bool attachDevice(uint8_t devAddr, uint32_t devHz,
                    i2c_master_dev_handle_t *outHandle);

 private:
  i2c_master_bus_handle_t bus_ = nullptr;
};

// ---- Device-level transfer helpers (8-bit register addresses) ----
bool i2cWriteRegs(i2c_master_dev_handle_t dev, uint8_t reg,
                  const uint8_t *data, size_t len);
bool i2cReadRegs(i2c_master_dev_handle_t dev, uint8_t reg,
                 uint8_t *data, size_t len);
// Raw transfers without a register address (for command-code devices such as SHTC3).
bool i2cRawWrite(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len);
bool i2cRawRead(i2c_master_dev_handle_t dev, uint8_t *data, size_t len);
bool i2cRawWriteThenRead(i2c_master_dev_handle_t dev,
                         const uint8_t *wr, size_t wrLen,
                         uint8_t *rd, size_t rdLen);
