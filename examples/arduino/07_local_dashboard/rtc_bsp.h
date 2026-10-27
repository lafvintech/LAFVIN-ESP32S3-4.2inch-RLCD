#pragma once

#include <stdint.h>
#include "i2c_bus_bsp.h"

// ============================================================
// rtc_bsp -- PCF85063 real-time clock driver
//
// Register layout (BCD, starting at 0x04, from the PCF85063 datasheet):
//   0x04 Seconds (bit7 = OS: oscillator stopped/interrupted; time is unreliable, not a battery-level flag)
//   0x05 Minutes
//   0x06 Hours (24-hour mode)
//   0x07 Day
//   0x08 Weekday (0=Sunday ... 6=Saturday)
//   0x09 Month
//   0x0A Year (00..99, interpreted as 2000..2099 in this example)
// ============================================================

struct RtcDateTime {
  uint16_t year;      // Full year, e.g. 2026.
  uint8_t month;
  uint8_t day;
  uint8_t weekday;    // 0=Sunday.
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

class Pcf85063 {
 public:
  bool begin(I2cBus &bus);

  // Read the time. A false result indicates I2C communication failure.
  // osFlag means the chip reports potentially unreliable time (see the header comments).
  // The data is still returned; the caller decides whether to trust or reset it.
  bool read(RtcDateTime *out, bool *osFlag = nullptr);

  // The application must validate the full date before calling, then read back OS/time after writing; I2C ACK alone is insufficient.
  bool write(const RtcDateTime &t);

 private:
  static uint8_t fromBcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
  static uint8_t toBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

  i2c_master_dev_handle_t dev_ = nullptr;
};
