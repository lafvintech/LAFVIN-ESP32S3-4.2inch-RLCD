#include <Arduino.h>
#include "buttons_bsp.h"

DebouncedButton::DebouncedButton(uint8_t pin, bool activeLow)
    : pin_(pin),
      activeLow_(activeLow),
      rawDown_(false),
      stableDown_(false),
      longFired_(false),
      shortPending_(false),
      longPending_(false),
      lastChangeMs_(0),
      downAtMs_(0) {}

bool DebouncedButton::readDown() const {
  bool down = (digitalRead(pin_) == LOW);
  if (!activeLow_) down = !down;
  return down;
}

void DebouncedButton::begin() {
  pinMode(pin_, activeLow_ ? INPUT_PULLUP : INPUT);
  rawDown_ = readDown();
  stableDown_ = rawDown_;
  longFired_ = false;
  shortPending_ = false;
  longPending_ = false;
  lastChangeMs_ = millis();
  downAtMs_ = lastChangeMs_;
}

void DebouncedButton::update() {
  const bool down = readDown();
  const uint32_t nowMs = millis();

  // On a level change, record the time and wait for the debounce window; mechanical contacts may bounce repeatedly.
  if (down != rawDown_) {
    rawDown_ = down;
    lastChangeMs_ = nowMs;
    return;
  }

  if ((nowMs - lastChangeMs_) >= kDebounceMs && down != stableDown_) {
    stableDown_ = down;
    if (stableDown_) {
      downAtMs_ = nowMs;
      longFired_ = false;
    } else if (!longFired_) {
      // A release before the threshold is a short press. A long press must not emit another SHORT on release.
      shortPending_ = true;
    }
  }

  // Emit a long press in the first update after the hold threshold, without waiting for release; precision depends on polling.
  if (stableDown_ && !longFired_ && (nowMs - downAtMs_) >= kLongPressMs) {
    longFired_ = true;
    longPending_ = true;
  }
}

ButtonEvent DebouncedButton::consumeEvent() {
  if (longPending_) {
    longPending_ = false;
    return BTN_LONG;
  }
  if (shortPending_) {
    shortPending_ = false;
    return BTN_SHORT;
  }
  return BTN_NONE;
}

uint32_t DebouncedButton::heldMs() const {
  if (!stableDown_) return 0;
  return millis() - downAtMs_;
}
