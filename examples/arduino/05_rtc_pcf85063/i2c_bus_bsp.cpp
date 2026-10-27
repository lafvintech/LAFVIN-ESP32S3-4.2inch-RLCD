#include <Arduino.h>
#include "i2c_bus_bsp.h"

bool I2cBus::begin(int pinSda, int pinScl, int port, uint32_t busHz) {
  if (bus_) return true;   // Already initialized
  (void)busHz;             // In this core version, each device's scl_speed_hz determines the bus speed.

  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = port;
  cfg.sda_io_num = (gpio_num_t)pinSda;
  cfg.scl_io_num = (gpio_num_t)pinScl;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;   // Weak internal pull-ups cannot replace suitable external pull-ups.

  return ESP_OK == i2c_new_master_bus(&cfg, &bus_);
}

bool I2cBus::attachDevice(uint8_t devAddr, uint32_t devHz,
                          i2c_master_dev_handle_t *outHandle) {
  if (!bus_ || !outHandle) return false;

  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = devAddr;
  dev.scl_speed_hz = devHz;
  return ESP_OK == i2c_master_bus_add_device(bus_, &dev, outHandle);
}

bool i2cWriteRegs(i2c_master_dev_handle_t dev, uint8_t reg,
                  const uint8_t *data, size_t len) {
  if (len > 30) return false;
  uint8_t buf[31];
  buf[0] = reg;
  memcpy(&buf[1], data, len);
  return ESP_OK == i2c_master_transmit(dev, buf, len + 1, 200);
}

bool i2cReadRegs(i2c_master_dev_handle_t dev, uint8_t reg,
                 uint8_t *data, size_t len) {
  // Write the register address, then read with a repeated START in one transaction, not two separate transfers.
  return ESP_OK == i2c_master_transmit_receive(dev, &reg, 1, data, len, 200);
}

bool i2cRawWrite(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len) {
  return ESP_OK == i2c_master_transmit(dev, data, len, 200);
}

bool i2cRawRead(i2c_master_dev_handle_t dev, uint8_t *data, size_t len) {
  return ESP_OK == i2c_master_receive(dev, data, len, 200);
}

bool i2cRawWriteThenRead(i2c_master_dev_handle_t dev,
                         const uint8_t *wr, size_t wrLen,
                         uint8_t *rd, size_t rdLen) {
  return ESP_OK == i2c_master_transmit_receive(dev, wr, wrLen, rd, rdLen, 200);
}
