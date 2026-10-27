#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include "ui_bsp.h"

namespace {
lv_obj_t *s_stateLabel = nullptr;
lv_obj_t *s_detailLabel = nullptr;
lv_obj_t *s_fileLabel = nullptr;
lv_obj_t *s_micLabel = nullptr;
lv_obj_t *s_micBar = nullptr;
lv_obj_t *s_playLabel = nullptr;
lv_obj_t *s_playBar = nullptr;
lv_obj_t *s_timeLabel = nullptr;

lv_obj_t *addLabel(lv_obj_t *parent, const char *text, int16_t x, int16_t y,
                   const lv_font_t *font = nullptr) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  if (font != nullptr) lv_obj_set_style_text_font(label, font, 0);
  return label;
}

void addRule(lv_obj_t *parent, int16_t y) {
  lv_obj_t *line = lv_obj_create(parent);
  lv_obj_remove_style_all(line);
  lv_obj_set_size(line, 372, 2);
  lv_obj_set_pos(line, 14, y);
  lv_obj_set_style_bg_color(line, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
}

lv_obj_t *addBar(lv_obj_t *parent, int16_t y) {
  lv_obj_t *bar = lv_bar_create(parent);
  lv_obj_set_pos(bar, 104, y);
  lv_obj_set_size(bar, 274, 16);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
  lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(bar, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
  return bar;
}

void formatTime(char *dst, size_t dstBytes, uint32_t millisValue) {
  const uint32_t totalSeconds = millisValue / 1000;
  snprintf(dst, dstBytes, "%02u:%02u", (unsigned)(totalSeconds / 60),
           (unsigned)(totalSeconds % 60));
}
}  // namespace

void audioUiBuild() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_remove_style_all(screen);
  lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(screen, lv_color_black(), 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  addLabel(screen, "VOICE RECORDER", 12, 5, &lv_font_montserrat_28);
  s_detailLabel = addLabel(screen, "SD WAV / 16 kHz / 16-bit / mono", 14, 42);
  addRule(screen, 64);

  addLabel(screen, "STATE", 18, 76);
  s_stateLabel = addLabel(screen, "IDLE", 18, 96, &lv_font_montserrat_28);
  s_fileLabel = addLabel(screen, "FILE  --", 18, 134);

  s_micLabel = addLabel(screen, "MIC    0%", 18, 164);
  s_micBar = addBar(screen, 164);
  s_playLabel = addLabel(screen, "PLAY   0%", 18, 196);
  s_playBar = addBar(screen, 196);
  s_timeLabel = addLabel(screen, "TIME  00:00 / 00:00", 18, 222);

  addRule(screen, 246);
  addLabel(screen, "HOLD BOOT : record until released", 18, 254);
  addLabel(screen, "TAP  KEY  : play / stop latest WAV", 18, 276);
}

void audioUiUpdate(const AudioUiData &data) {
  lv_label_set_text(s_stateLabel, data.state ? data.state : "--");
  lv_label_set_text(s_detailLabel, data.detail ? data.detail : "");

  char line[64];
  snprintf(line, sizeof(line), "FILE  %s",
           data.fileName ? data.fileName : "--");
  lv_label_set_text(s_fileLabel, line);

  snprintf(line, sizeof(line), "MIC  %3u%%", data.micLevelPercent);
  lv_label_set_text(s_micLabel, line);
  lv_bar_set_value(s_micBar, data.micLevelPercent, LV_ANIM_OFF);

  snprintf(line, sizeof(line), "PLAY %3u%%", data.playbackPercent);
  lv_label_set_text(s_playLabel, line);
  lv_bar_set_value(s_playBar, data.playbackPercent, LV_ANIM_OFF);

  char elapsed[8];
  char total[8];
  formatTime(elapsed, sizeof(elapsed), data.elapsedMs);
  if (data.showTotal) {
    formatTime(total, sizeof(total), data.totalMs);
  } else {
    snprintf(total, sizeof(total), "--:--");
  }
  snprintf(line, sizeof(line), "TIME  %s / %s", elapsed, total);
  lv_label_set_text(s_timeLabel, line);
}
