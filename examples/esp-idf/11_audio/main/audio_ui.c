#include "audio_ui.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"

/* Snapshot-consumption polling interval; distinct from the audio publication and LVGL redraw intervals. */
#define UI_POLL_INTERVAL_MS 100

static QueueHandle_t s_updates;
static lv_obj_t *s_state;
static lv_obj_t *s_detail;
static lv_obj_t *s_file;
static lv_obj_t *s_mic_label;
static lv_obj_t *s_mic_bar;
static lv_obj_t *s_play_label;
static lv_obj_t *s_play_bar;
static lv_obj_t *s_time;

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

static void add_rule(lv_obj_t *parent, int16_t y)
{
    lv_obj_t *rule = lv_obj_create(parent);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, 372, 2);
    lv_obj_set_pos(rule, 14, y);
    lv_obj_set_style_bg_color(rule, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
}

static lv_obj_t *add_bar(lv_obj_t *parent, int16_t y)
{
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

static void format_time(char *buffer, size_t capacity, uint32_t millis)
{
    const uint32_t seconds = millis / 1000;
    snprintf(buffer, capacity, "%02lu:%02lu",
             (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
}

static void build_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    add_label(screen, "VOICE RECORDER", 12, 5, &lv_font_montserrat_28);
    s_detail = add_label(screen, "Starting SD and codecs...", 14, 42, NULL);
    lv_obj_set_width(s_detail, 372);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_MODE_DOTS);
    add_rule(screen, 64);
    add_label(screen, "STATE", 18, 76, NULL);
    s_state = add_label(screen, "IDLE", 18, 96, &lv_font_montserrat_28);
    s_file = add_label(screen, "FILE  --", 18, 134, NULL);
    s_mic_label = add_label(screen, "MIC    0%", 18, 164, NULL);
    s_mic_bar = add_bar(screen, 164);
    s_play_label = add_label(screen, "PLAY   0%", 18, 196, NULL);
    s_play_bar = add_bar(screen, 196);
    s_time = add_label(screen, "TIME  00:00 / 00:00", 18, 222, NULL);
    add_rule(screen, 246);
    add_label(screen, "HOLD BOOT : record until released", 18, 254, NULL);
    add_label(screen, "TAP  KEY  : play / stop / retry", 18, 276, NULL);
}

/* Called only by the LVGL task; labels copy strings rather than retaining addresses into the local snapshot. */
static void render_snapshot(const audio_ui_data_t *data)
{
    lv_label_set_text(s_state, data->state);
    lv_label_set_text(s_detail, data->detail);
    lv_label_set_text_fmt(s_file, "FILE  %s", data->file_name);
    lv_label_set_text_fmt(s_mic_label, "MIC  %3u%%", (unsigned)data->mic_percent);
    lv_bar_set_value(s_mic_bar, data->mic_percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_play_label, "PLAY %3u%%", (unsigned)data->playback_percent);
    lv_bar_set_value(s_play_bar, data->playback_percent, LV_ANIM_OFF);
    char elapsed[16];
    char total[16];
    format_time(elapsed, sizeof(elapsed), data->elapsed_ms);
    if (data->show_total) {
        format_time(total, sizeof(total), data->total_ms);
    } else {
        snprintf(total, sizeof(total), "--:--");
    }
    lv_label_set_text_fmt(s_time, "TIME  %s / %s", elapsed, total);
}

static void consume_snapshot(lv_timer_t *timer)
{
    (void)timer;
    audio_ui_data_t data;
    /* LVGL timers already hold the port lock; do not lock again. Fetch without waiting and return immediately if unchanged.
      * Do not create a second UI task or let the audio task modify widgets across task boundaries. */
    if (xQueueReceive(s_updates, &data, 0) == pdTRUE) {
        render_snapshot(&data);
    }
}

esp_err_t audio_ui_init(void)
{
    if (s_updates != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_updates = xQueueCreate(1, sizeof(audio_ui_data_t));
    if (s_updates == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (lv_timer_create(consume_snapshot, UI_POLL_INTERVAL_MS, NULL) == NULL) {
        vQueueDelete(s_updates);
        s_updates = NULL;
        return ESP_ERR_NO_MEM;
    }
    build_screen();
    return ESP_OK;
}

esp_err_t audio_ui_publish(const audio_ui_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_updates == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* The single slot intentionally keeps only the latest full state, overwriting old snapshots; buttons use a separate event queue. */
    return xQueueOverwrite(s_updates, data) == pdPASS ? ESP_OK : ESP_FAIL;
}
