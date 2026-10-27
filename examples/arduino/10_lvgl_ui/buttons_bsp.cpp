#include <Arduino.h>
#include "buttons_bsp.h"

DebouncedButton::DebouncedButton(uint8_t pin, bool activeLow)
    : pin_(pin),
      activeLow_(activeLow),
      rawDown_(false),
      stableDown_(false),
      lastChangeMs_(0),
      pressEdgePending_(0) {}

void DebouncedButton::begin() {
  pinMode(pin_, activeLow_ ? INPUT_PULLUP : INPUT);

  bool down = (digitalRead(pin_) == LOW);
  if (!activeLow_) down = !down;
  rawDown_ = down;
  stableDown_ = down;
  lastChangeMs_ = millis();
  pressEdgePending_ = 0;
}

void DebouncedButton::update() {
  bool down = (digitalRead(pin_) == LOW);
  if (!activeLow_) down = !down;

  const uint32_t nowMs = millis();
  if (down != rawDown_) {
    rawDown_ = down;
    lastChangeMs_ = nowMs;
    return;
  }

  if ((nowMs - lastChangeMs_) < kDebounceMs || down == stableDown_) return;

  stableDown_ = down;
  if (stableDown_ && pressEdgePending_ != UINT8_MAX) {
    pressEdgePending_++;
  }
}

bool DebouncedButton::consumePress() {
  if (pressEdgePending_ == 0) return false;
  pressEdgePending_--;
  return true;
}
