#pragma once

#include <stdint.h>

// ============================================================
// buttons_bsp -- button edge detection with software debouncing
//
// Mechanical buttons bounce between high and low during press/release.
// Treating every transition as a press would trigger duplicates. Instead:
//   Record each level change and accept it only after it stays stable for DEBOUNCE_MS.
// consumePress() returns true once per "released -> pressed" transition,
// which suits single-click actions such as switching pages.
// ============================================================

class DebouncedButton {
 public:
  DebouncedButton(uint8_t pin, bool activeLow = true);

  // Call update frequently from loop; no background task is created, so long blocks may miss short presses.
  void update();

  // Consume one confirmed press edge; holding the button generates no repeated edges.
  bool consumePress();

  bool isDown() const { return stableDown_; }

 private:
  uint8_t pin_;
  bool activeLow_;
  bool rawDown_;        // Raw state from the previous update.
  bool stableDown_;     // Confirmed debounced state.
  uint32_t lastChangeMs_;
  uint32_t pressEdgePending_;

  static const uint16_t kDebounceMs = 30;
};
