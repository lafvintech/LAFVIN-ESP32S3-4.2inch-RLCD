#pragma once

#include <stdint.h>

class RlcdPanel;

// ============================================================
// lvgl_port -- Bridge between LVGL 9 and the custom ST7305 driver
//
// LVGL generates I1 pixels; this layer converts layout; RlcdPanel handles packing and SPI transfer.
// Initialization, UI updates, and the handler run in the same Arduino loop task; concurrent access is unsupported.
// ============================================================

// Call once after panel.begin() succeeds; retain the panel and render buffer for the program's lifetime.
bool lvglPortBegin(RlcdPanel *panel);

// Run LVGL once, then delay if needed; maxMs limits only the delay, not rendering time.
void lvglPortTask(uint32_t maxMs);
