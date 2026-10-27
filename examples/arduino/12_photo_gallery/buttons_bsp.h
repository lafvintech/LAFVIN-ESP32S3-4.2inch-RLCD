#pragma once

#include <stdint.h>

// ============================================================
// buttons_bsp -- Debounce and short/long-press detection
//
// Debounce mechanical buttons before distinguishing gestures:
//   Short press: release before the threshold; emits one SHORT on release
//   Long press: hold past the threshold; emits one LONG without waiting for release
//
// The recorder uses a BOOT long press to start immediately and stops when
// the button is released. A long press never produces an extra short press.
// ============================================================

enum ButtonEvent : uint8_t {
  BTN_NONE = 0,
  BTN_SHORT = 1,
  BTN_LONG = 2,
};

class DebouncedButton {
 public:
  DebouncedButton(uint8_t pin, bool activeLow = true);

  void begin();
  void update();

  ButtonEvent consumeEvent();
  bool isDown() const { return stableDown_; }
  uint32_t heldMs() const;

 private:
  uint8_t pin_;
  bool activeLow_;
  bool rawDown_;
  bool stableDown_;
  bool longFired_;
  bool shortPending_;
  bool longPending_;
  uint32_t lastChangeMs_;
  uint32_t downAtMs_;

  static const uint16_t kDebounceMs = 30;
  static const uint16_t kLongPressMs = 700;

  bool readDown() const;
};
