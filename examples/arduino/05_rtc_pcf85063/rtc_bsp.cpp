#include <Arduino.h>
#include "board_pins.h"
#include "rtc_bsp.h"

bool Pcf85063::begin(I2cBus &bus) {
  // The PCF85063 datasheet specifies support for up to 400 kHz.
  if (!bus.attachDevice(I2C_ADDR_PCF85063, 300000, &dev_)) return false;

  // Control_1: bit5=STOP, bit1=12_24. Preserve other function bits; ensure the clock runs
  // in 24-hour format to avoid misreading a chip previously set to 12-hour format by another example.
  uint8_t control1 = 0;
  if (!i2cReadRegs(dev_, 0x00, &control1, 1)) return false;
  const uint8_t wanted = control1 & (uint8_t)~((1U << 5) | (1U << 1));
  return wanted == control1 || i2cWriteRegs(dev_, 0x00, &wanted, 1);
}

bool Pcf85063::read(RtcDateTime *out, bool *osFlag) {
  if (!dev_ || !out) return false;

  // Read all 7 registers from 0x04 in one transaction to avoid inconsistent fields across a second boundary.
  uint8_t raw[7];
  if (!i2cReadRegs(dev_, 0x04, raw, sizeof(raw))) return false;

  bool osSet = (raw[0] & 0x80) != 0;   // Seconds register bit7 = OS flag

  // BCD 0x59 means decimal 59. Mask status bits before conversion; reading does not clear OS.
  out->second = fromBcd(raw[0] & 0x7F);
  out->minute = fromBcd(raw[1] & 0x7F);
  out->hour = fromBcd(raw[2] & 0x3F);
  out->day = fromBcd(raw[3] & 0x3F);
  out->weekday = raw[4] & 0x07;
  out->month = fromBcd(raw[5] & 0x1F);
  uint8_t year2 = fromBcd(raw[6]);
  // The PCF85063 month register has no century bit; the course examples assume
  // 2000-2099. The application maintains this century convention; the chip stores only a two-digit year.
  out->year = (uint16_t)(2000 + year2);

  if (osFlag) *osFlag = osSet;
  return true;
}

bool Pcf85063::write(const RtcDateTime &t) {
  if (!dev_ || t.year < 2000 || t.year > 2099) return false;

  uint8_t year2 = t.year % 100;
  uint8_t weekday = t.weekday > 6 ? 0 : t.weekday;

  uint8_t regs[7];
  regs[0] = toBcd(t.second) & 0x7F;    // Write bit7 as 0; the seconds value remains t.second.
  regs[1] = toBcd(t.minute);
  regs[2] = toBcd(t.hour);             // 24-hour format (keep bit7 at 0)
  regs[3] = toBcd(t.day);
  regs[4] = weekday;
  regs[5] = toBcd(t.month);
  regs[6] = toBcd(year2);

  // Write seconds through year together; the application still reads back to check validity rather than assuming success in the driver.
  return i2cWriteRegs(dev_, 0x04, regs, sizeof(regs));
}
