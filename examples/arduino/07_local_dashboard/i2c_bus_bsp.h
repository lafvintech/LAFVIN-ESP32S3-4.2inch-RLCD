#pragma once

#include <stdint.h>
#include <driver/i2c_master.h>

// ============================================================
// i2c_bus_bsp -- course I2C bus wrapper based on the ESP-IDF i2c_master API
//
// This example uses ESP-IDF i2c_master: multiple devices share one bus; drivers only borrow handles.
// Do not initialize the same controller again with Wire or another driver. Each example runs independently;
// global resources need not be shared between examples. This wrapper retains its handles until restart.
// ============================================================

class I2cBus {
 public:
  // See board_pins.h for sda/scl; busHz is reserved. Each device's devHz sets its actual speed.
  bool begin(int pinSda, int pinScl, int port = 0, uint32_t busHz = 400000);

  // Register a device without probing for an ACK; devAddr is a 7-bit address (e.g. 0x70), not left-shifted.
  bool attachDevice(uint8_t devAddr, uint32_t devHz,
                    i2c_master_dev_handle_t *outHandle);

 private:
  i2c_master_bus_handle_t bus_ = nullptr;
};

// ---- Device-level helpers (8-bit register addresses) ----
bool i2cWriteRegs(i2c_master_dev_handle_t dev, uint8_t reg,
                  const uint8_t *data, size_t len);
bool i2cReadRegs(i2c_master_dev_handle_t dev, uint8_t reg,
                 uint8_t *data, size_t len);
// Raw transfers without a register address, for command-code devices such as SHTC3.
bool i2cRawWrite(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len);
bool i2cRawRead(i2c_master_dev_handle_t dev, uint8_t *data, size_t len);
bool i2cRawWriteThenRead(i2c_master_dev_handle_t dev,
                         const uint8_t *wr, size_t wrLen,
                         uint8_t *rd, size_t rdLen);
