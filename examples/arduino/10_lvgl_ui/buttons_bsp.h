#pragma once

#include <stdint.h>

// ============================================================
// buttons_bsp -- Software debounce for a single user button
//
// KEY on GPIO18 is active low. consumePress() returns true once after a debounced press,
// without waiting for release. Holding the button does not repeat the event, suitable for discrete actions such as next-page navigation.
// ============================================================

class DebouncedButton {
 public:
  DebouncedButton(uint8_t pin, bool activeLow = true);

  // Call in setup() to enable the input pull-up and synchronize the initial level.
  void begin();

  // Call repeatedly from the main loop.
  void update();

  // Consume one debounced press edge.
  bool consumePress();

 private:
  uint8_t pin_;
  bool activeLow_;
  bool rawDown_;
  bool stableDown_;
  uint32_t lastChangeMs_;
  uint8_t pressEdgePending_;

  static const uint16_t kDebounceMs = 30;
};
