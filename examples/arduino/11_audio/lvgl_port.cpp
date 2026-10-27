#include <Arduino.h>
#include <lvgl.h>
#include "display_bsp.h"
#include "lvgl_port.h"

// Configuration guard: without lv_conf.h in the library directory, LVGL defaults to
// RGB565 and produces an incorrect image. Report a clear error instead of silently continuing.
#if LV_COLOR_DEPTH != 1
#error "lv_conf.h not picked up! Copy examples/arduino/11_audio/lv_conf.h next to the lvgl library folder (see docs/11_audio.md)"
#endif

namespace {

RlcdPanel *s_panel = nullptr;
lv_display_t *s_disp = nullptr;

// LVGL reads millis() directly; the main loop does not call lv_tick_inc().
uint32_t tickFromMillis() {
  return millis();
}

// ------------------------------------------------------------
// lv_timer_handler() invokes the flush callback synchronously; FULL mode renders complete frames.
//
// In I1 format, the first 8 bytes of px_map hold LVGL's two-color palette, followed by
// packed pixels: 8 horizontal pixels per byte, with the MSB on the left.
// A pixel is 1 when luminance exceeds LV_DRAW_SW_I1_LUM_THRESHOLD,
// so 1 means white paper and 0 means black ink. If colors are inverted on the board,
// invert kBitIsWhite; no other changes are needed.
// ------------------------------------------------------------
const bool kBitIsWhite = true;

void flushToRlcd(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) {
  pxMap += 8;   // Skip LVGL I1's 2 x 4-byte palette

  const int32_t areaW = area->x2 - area->x1 + 1;
  const int32_t strideBytes = (areaW + 7) / 8;

  for (int32_t y = area->y1; y <= area->y2; y++) {
    const uint8_t *row = pxMap + (y - area->y1) * strideBytes;
    for (int32_t x = area->x1; x <= area->x2; x++) {
      uint8_t byteVal = row[(x - area->x1) >> 3];
      bool bitSet = byteVal & (0x80 >> ((x - area->x1) & 7));
      bool white = kBitIsWhite ? bitSet : !bitSet;
      s_panel->setPixel((int16_t)x, (int16_t)y,
                        white ? LCD_PIXEL_WHITE : LCD_PIXEL_BLACK);
    }
  }

  s_panel->flush();                 // Submit the separate panel framebuffer to SPI
  // I1 source pixels have been copied, so LVGL can reuse pxMap; SPI may still be transmitting.
  // The panel framebuffer remains subject to the transfer-time write restrictions in display_bsp.h.
  lv_display_flush_ready(disp);
}

}  // namespace

bool lvglPortBegin(RlcdPanel *panel) {
  if (!panel || !panel->isReady()) return false;
  s_panel = panel;

  lv_init();
  lv_tick_set_cb(tickFromMillis);

  s_disp = lv_display_create(panel->width(), panel->height());
  lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_I1);

  // Render buffer: 400*300/8 = 15000 pixel bytes; I1 also requires another 8 bytes
  // for its two-color palette. Omitting them causes out-of-bounds access and display noise.
  // One LVGL render buffer plus one panel-packing buffer is not LVGL double buffering.
  // Layouts and black/white polarity differ, so memcpy is insufficient; both buffers live for the program's lifetime.
  size_t pixelBytes = ((size_t)panel->width() + 7) / 8 * panel->height();
  size_t bufBytes = pixelBytes + 8;
  uint8_t *buf = (uint8_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
  if (buf == nullptr) {
    Serial.println(F("[lvgl] render buffer alloc FAILED"));
    return false;
  }
  lv_display_set_buffers(s_disp, buf, nullptr, bufBytes,
                         LV_DISPLAY_RENDER_MODE_FULL);

  lv_display_set_flush_cb(s_disp, flushToRlcd);

  // Create the monochrome theme, then explicitly install it on the display; init alone does not activate it.
  lv_theme_t *monoTheme =
      lv_theme_mono_init(s_disp, false, &lv_font_montserrat_14);
  lv_display_set_theme(s_disp, monoTheme);

  return true;
}

void lvglPortTask(uint32_t maxMs) {
  uint32_t nextMs = lv_timer_handler();
  if (nextMs > maxMs) nextMs = maxMs;
  delay(nextMs);
}

void lvglPortPoll() {
  // Only omits the extra delay; UI work stays in the foreground and still consumes audio-loop time.
  lv_timer_handler();
}
