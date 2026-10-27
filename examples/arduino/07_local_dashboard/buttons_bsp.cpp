#include <Arduino.h>
#include "buttons_bsp.h"

DebouncedButton::DebouncedButton(uint8_t pin, bool activeLow)
    : pin_(pin),
      activeLow_(activeLow),
      rawDown_(false),
      stableDown_(false),
      lastChangeMs_(0),
      pressEdgePending_(0) {
  pinMode(pin_, activeLow_ ? INPUT_PULLUP : INPUT);
}

void DebouncedButton::update() {
  bool level = (digitalRead(pin_) == LOW);   // Active low: pressed.
  if (!activeLow_) level = !level;

  uint32_t now = millis();
  if (level != rawDown_) {
    rawDown_ = level;
    lastChangeMs_ = now;
    return;   // The level just changed; wait for it to stabilize.
  }

  if ((now - lastChangeMs_) >= kDebounceMs && level != stableDown_) {
    // A stable state differs from the last confirmed state: accept the transition.
    stableDown_ = level;
    if (stableDown_) pressEdgePending_++;
  }
}

bool DebouncedButton::consumePress() {
  if (pressEdgePending_ > 0) {
    pressEdgePending_--;
    return true;
  }
  return false;
}
