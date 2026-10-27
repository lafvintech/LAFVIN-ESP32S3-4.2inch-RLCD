#include "ui.h"

#include <stdint.h>
#include <stdio.h>

#include "assets/image_resources.h"
#include "esp_log.h"
#include "lvgl.h"

#define PAGE_COUNT 2
#define PHOTO_COUNT 2

static lv_obj_t *s_pages[PAGE_COUNT];
static lv_obj_t *s_checkbox;
static lv_obj_t *s_switch;
static lv_obj_t *s_slider;
static lv_obj_t *s_bar;
static lv_obj_t *s_value_label;
static lv_obj_t *s_photo_image;
static lv_obj_t *s_photo_label;
static uint8_t s_page;
static uint8_t s_value = 60;
static uint8_t s_photo;
/* LVGL renders RGB565 images to I1; descriptors and pixels must remain resident because set_src does not copy the full resource. */
static const lv_image_dsc_t *const s_photos[PHOTO_COUNT] = {&img1, &img2};

static lv_obj_t *add_label(
    lv_obj_t *parent, const char *text, int16_t x, int16_t y, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    if (font != NULL) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    return label;
}

static lv_obj_t *add_overlay(
    lv_obj_t *parent, const char *text, lv_align_t align, int16_t x, int16_t y)
{
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

static void update_widgets(void)
{
    lv_slider_set_value(s_slider, s_value, LV_ANIM_OFF);
    lv_bar_set_value(s_bar, s_value, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_value_label, "AUTO: %u%%", (unsigned)s_value);
    if (s_value >= 50) {
        lv_obj_add_state(s_checkbox, LV_STATE_CHECKED);
        lv_obj_add_state(s_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_checkbox, LV_STATE_CHECKED);
        lv_obj_remove_state(s_switch, LV_STATE_CHECKED);
    }
}

/* LVGL timers run inside the handler with the lock already held; do not reacquire the non-recursive lock.
  * Widget values keep updating while hidden; the photo timer changes images only when the photo page is visible. */
static void widget_timer(lv_timer_t *timer)
{
    (void)timer;
    s_value = s_value >= 100 ? 0 : (uint8_t)(s_value + 10);
    update_widgets();
}

static void photo_timer(lv_timer_t *timer)
{
    (void)timer;
    if (s_page != 1) {
        return;
    }
    s_photo = (uint8_t)((s_photo + 1) % PHOTO_COUNT);
    lv_image_set_src(s_photo_image, s_photos[s_photo]);
    lv_label_set_text_fmt(s_photo_label, "PHOTO %u / %u",
                         (unsigned)(s_photo + 1), (unsigned)PHOTO_COUNT);
}

static void build_widgets(lv_obj_t *page)
{
    add_label(page, "LVGL V9", 12, 8, &lv_font_montserrat_28);
    lv_obj_t *number = lv_label_create(page);
    lv_label_set_text(number, "PAGE 1 / 2");
    lv_obj_align(number, LV_ALIGN_TOP_RIGHT, -12, 16);
    add_label(page, "Classic widgets - automatic demo", 14, 48, NULL);

    lv_obj_t *rule = lv_obj_create(page);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, 372, 2);
    lv_obj_set_pos(rule, 14, 70);
    lv_obj_set_style_bg_color(rule, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    lv_obj_t *button = lv_button_create(page);
    lv_obj_set_size(button, 112, 46);
    lv_obj_set_pos(button, 18, 84);
    lv_obj_t *text = lv_label_create(button);
    lv_label_set_text(text, "BUTTON");
    lv_obj_center(text);

    s_checkbox = lv_checkbox_create(page);
    lv_checkbox_set_text(s_checkbox, "CHECKBOX");
    lv_obj_set_pos(s_checkbox, 154, 96);
    s_switch = lv_switch_create(page);
    lv_obj_set_size(s_switch, 58, 30);
    lv_obj_set_pos(s_switch, 308, 84);
    add_label(page, "SWITCH", 306, 118, NULL);

    add_label(page, "SLIDER", 18, 154, NULL);
    s_slider = lv_slider_create(page);
    lv_obj_set_size(s_slider, 224, 16);
    lv_obj_set_pos(s_slider, 88, 154);
    lv_slider_set_range(s_slider, 0, 100);
    s_value_label = add_label(page, "AUTO: 60%", 316, 154, NULL);
    add_label(page, "BAR", 18, 204, NULL);
    s_bar = lv_bar_create(page);
    lv_obj_set_size(s_bar, 224, 16);
    lv_obj_set_pos(s_bar, 88, 204);
    lv_bar_set_range(s_bar, 0, 100);
    add_label(page, "KEY: OPEN PHOTO PAGE", 18, 266, NULL);
    update_widgets();
}

/* Both pages are persistent containers; switching only changes visibility, preserving widgets and the current photo. */
static void show_page(uint8_t index)
{
    s_page = index % PAGE_COUNT;
    for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
        if (i == s_page) {
            lv_obj_remove_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    ESP_LOGI("lvgl_demo", "page %u/%u", (unsigned)(s_page + 1), (unsigned)PAGE_COUNT);
}

void demo_ui_build(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
        s_pages[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(s_pages[i]);
        lv_obj_set_size(s_pages[i], LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(s_pages[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(s_pages[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(s_pages[i], lv_color_black(), 0);
        lv_obj_remove_flag(s_pages[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    build_widgets(s_pages[0]);
    s_photo_image = lv_image_create(s_pages[1]);
    lv_image_set_src(s_photo_image, s_photos[0]);
    lv_obj_center(s_photo_image);
    s_photo_label = add_overlay(s_pages[1], "PHOTO 1 / 2", LV_ALIGN_TOP_LEFT, 8, 8);
    add_overlay(s_pages[1], "KEY: WIDGETS", LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_timer_create(widget_timer, 1000, NULL);
    lv_timer_create(photo_timer, 3000, NULL);
    show_page(0);
}

void demo_ui_next_page(void)
{
    show_page((uint8_t)(s_page + 1));
}
