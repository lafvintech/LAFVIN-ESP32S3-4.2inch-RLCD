#include <Arduino.h>
#include <lvgl.h>
#include "image_resources.h"
#include "ui_bsp.h"

namespace {
constexpr uint8_t kPageCount = 2;

lv_obj_t *s_pages[kPageCount] = {};
lv_obj_t *s_checkbox = nullptr;
lv_obj_t *s_switch = nullptr;
lv_obj_t *s_slider = nullptr;
lv_obj_t *s_bar = nullptr;
lv_obj_t *s_valueLabel = nullptr;
lv_obj_t *s_photoImage = nullptr;
lv_obj_t *s_photoLabel = nullptr;

uint8_t s_currentPage = 0;
uint8_t s_demoValue = 60;
uint8_t s_photoIndex = 0;

// LVGL retains image descriptor/pixel pointers, so use static resources rather than temporary arrays.
const lv_image_dsc_t *const s_photos[] = {&img1, &img2};
constexpr uint8_t kPhotoCount = sizeof(s_photos) / sizeof(s_photos[0]);

void preparePage(lv_obj_t *page) {
  lv_obj_remove_style_all(page);
  lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(page, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(page, lv_color_black(), 0);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *addLabel(lv_obj_t *parent, const char *text,
                   int16_t x, int16_t y, const lv_font_t *font = nullptr) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  if (font != nullptr) {
    lv_obj_set_style_text_font(label, font, 0);
  }
  return label;
}

lv_obj_t *addOverlayLabel(lv_obj_t *parent, const char *text,
                          lv_align_t align, int16_t x, int16_t y) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_bg_color(label, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(label, lv_color_black(), 0);
  lv_obj_set_style_border_width(label, 1, 0);
  lv_obj_set_style_radius(label, 0, 0);
  lv_obj_set_style_pad_all(label, 4, 0);
  lv_obj_align(label, align, x, y);
  return label;
}

void updateWidgetValues() {
  lv_slider_set_value(s_slider, s_demoValue, LV_ANIM_OFF);
  lv_bar_set_value(s_bar, s_demoValue, LV_ANIM_OFF);

  char text[20];
  snprintf(text, sizeof(text), "AUTO: %u%%", (unsigned)s_demoValue);
  lv_label_set_text(s_valueLabel, text);

  // Values of 50 or greater select the widget, demonstrating dynamic state changes.
  if (s_demoValue >= 50) {
    lv_obj_add_state(s_checkbox, LV_STATE_CHECKED);
    lv_obj_add_state(s_switch, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(s_checkbox, LV_STATE_CHECKED);
    lv_obj_remove_state(s_switch, LV_STATE_CHECKED);
  }
}

// LVGL timers run inside loop's lv_timer_handler, not in a separate task; hidden pages also update.
void widgetTimerCb(lv_timer_t *) {
  s_demoValue = (s_demoValue >= 100) ? 0 : (uint8_t)(s_demoValue + 10);
  updateWidgetValues();
}

void photoTimerCb(lv_timer_t *) {
  // The timer keeps running, but photos do not switch off-page and page-entry timing is not reset.
  if (s_currentPage != 1) return;

  s_photoIndex = (uint8_t)((s_photoIndex + 1) % kPhotoCount);
  lv_image_set_src(s_photoImage, s_photos[s_photoIndex]);

  char text[20];
  snprintf(text, sizeof(text), "PHOTO %u / %u",
           (unsigned)(s_photoIndex + 1), (unsigned)kPhotoCount);
  lv_label_set_text(s_photoLabel, text);
  Serial.printf("[10] photo %u/%u\n",
                (unsigned)(s_photoIndex + 1), (unsigned)kPhotoCount);
}

void buildWidgetPage(lv_obj_t *page) {
  // Label: the most commonly used text widget.
  addLabel(page, "LVGL V9", 12, 8, &lv_font_montserrat_28);

  lv_obj_t *pageNumber = lv_label_create(page);
  lv_label_set_text(pageNumber, "PAGE 1 / 2");
  lv_obj_align(pageNumber, LV_ALIGN_TOP_RIGHT, -12, 16);

  addLabel(page, "Classic widgets - automatic demo", 14, 48);

  lv_obj_t *separator = lv_obj_create(page);
  lv_obj_remove_style_all(separator);
  lv_obj_set_size(separator, 372, 2);
  lv_obj_set_pos(separator, 14, 70);
  lv_obj_set_style_bg_color(separator, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);

  // Button: place a Label inside the container, demonstrating LVGL's standard parent-child structure.
  lv_obj_t *button = lv_button_create(page);
  lv_obj_set_size(button, 112, 46);
  lv_obj_set_pos(button, 18, 84);
  lv_obj_t *buttonText = lv_label_create(button);
  lv_label_set_text(buttonText, "BUTTON");
  lv_obj_center(buttonText);

  // Checkbox: includes its own text and checked state.
  s_checkbox = lv_checkbox_create(page);
  lv_checkbox_set_text(s_checkbox, "CHECKBOX");
  lv_obj_set_pos(s_checkbox, 154, 96);

  // Switch: the checked state indicates whether it is on.
  s_switch = lv_switch_create(page);
  lv_obj_set_size(s_switch, 58, 30);
  lv_obj_set_pos(s_switch, 308, 84);
  addLabel(page, "SWITCH", 306, 118);

  addLabel(page, "SLIDER", 18, 154);
  s_slider = lv_slider_create(page);
  lv_obj_set_size(s_slider, 224, 16);
  lv_obj_set_pos(s_slider, 88, 154);
  lv_slider_set_range(s_slider, 0, 100);

  s_valueLabel = addLabel(page, "AUTO: 60%", 316, 154);

  addLabel(page, "BAR", 18, 204);
  s_bar = lv_bar_create(page);
  lv_obj_set_size(s_bar, 224, 16);
  lv_obj_set_pos(s_bar, 88, 204);
  lv_bar_set_range(s_bar, 0, 100);

  addLabel(page, "KEY: OPEN PHOTO PAGE", 18, 266);
  updateWidgetValues();
}

void buildPhotoPage(lv_obj_t *page) {
  s_photoImage = lv_image_create(page);
  lv_image_set_src(s_photoImage, s_photos[s_photoIndex]);
  lv_obj_center(s_photoImage);

  s_photoLabel = addOverlayLabel(page, "PHOTO 1 / 2",
                                 LV_ALIGN_TOP_LEFT, 8, 8);
  addOverlayLabel(page, "KEY: WIDGETS", LV_ALIGN_BOTTOM_RIGHT, -8, -8);
}

// Reuse the two existing widget trees to avoid allocating/freeing objects on each page switch.
void showPage(uint8_t pageIndex) {
  s_currentPage = pageIndex % kPageCount;
  for (uint8_t i = 0; i < kPageCount; i++) {
    if (i == s_currentPage) {
      lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  Serial.printf("[10] page %u/%u\n", (unsigned)(s_currentPage + 1),
                (unsigned)kPageCount);
}
}  // namespace

void lvglDemoBuild() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
  lv_obj_set_style_text_color(screen, lv_color_black(), 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  for (uint8_t i = 0; i < kPageCount; i++) {
    s_pages[i] = lv_obj_create(screen);
    preparePage(s_pages[i]);
  }

  buildWidgetPage(s_pages[0]);
  buildPhotoPage(s_pages[1]);

  // The timer updates Slider, Bar, Checkbox, and Switch once per second.
  lv_timer_create(widgetTimerCb, 1000, nullptr);
  // Page 2 switches photos every 3 seconds; leaving that page preserves the current photo.
  lv_timer_create(photoTimerCb, 3000, nullptr);
  showPage(0);
}

void lvglDemoNextPage() {
  showPage((uint8_t)(s_currentPage + 1));
}
