#pragma once

#include <stdint.h>
#include "i2c_bus_bsp.h"

// ============================================================
// rtc_bsp -- PCF85063 real-time clock driver
//
// Register layout (BCD, starting at 0x04; from the PCF85063 datasheet):
//   0x04 seconds (bit7 = OS: the oscillator has stopped or been interrupted; time is untrusted, not a battery-level flag)
//   0x05 minutes
//   0x06 hours (24-hour format)
//   0x07 day
//   0x08 weekday (0=Sunday ... 6=Saturday)
//   0x09 month
//   0x0A year (00-99, interpreted as 2000-2099 in this example)
// ============================================================

struct RtcDateTime {
  uint16_t year;      // Full year, e.g. 2026
  uint8_t month;
  uint8_t day;
  uint8_t weekday;    // 0=Sunday
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

class Pcf85063 {
 public:
  bool begin(I2cBus &bus);

  // Read the time. Returns false on I2C communication failure.
  // A set osFlag means the chip reports potentially inaccurate time (see the header notes).
  // The data is still returned; the caller decides whether to trust it or set the time again.
  bool read(RtcDateTime *out, bool *osFlag = nullptr);

  // The application checks the complete date range before calling; this driver only bounds the year and clears OS when writing seconds.
  // Read back OS and the date after writing; a true return value alone does not establish valid time.
  bool write(const RtcDateTime &t);

 private:
  static uint8_t fromBcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
  static uint8_t toBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

  i2c_master_dev_handle_t dev_ = nullptr;
};
