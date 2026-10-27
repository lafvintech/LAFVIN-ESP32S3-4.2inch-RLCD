#include "button_service.h"

#include <stdbool.h>

#include "board_buttons.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    bool raw_pressed;
    bool stable_pressed;
    bool long_press_sent;
    int64_t changed_at_us;
    int64_t pressed_at_us;
} debounce_state_t;

static button_service_config_t s_config;
static TaskHandle_t s_task;

static void update_button(
    debounce_state_t *state,
    bool pressed,
    button_service_event_t pressed_event,
    button_service_event_t released_event,
    button_service_event_t long_pressed_event,
    int64_t now_us)
{
    if (pressed != state->raw_pressed) {
        state->raw_pressed = pressed;
        state->changed_at_us = now_us;
        return;
    }

    const int64_t debounce_us = (int64_t)s_config.debounce_ms * 1000;
    if (pressed != state->stable_pressed &&
        now_us - state->changed_at_us >= debounce_us) {
        state->stable_pressed = pressed;
        if (pressed) {
            state->pressed_at_us = now_us;
            state->long_press_sent = false;
            s_config.handler(pressed_event, s_config.context);
        } else {
            state->long_press_sent = false;
            s_config.handler(released_event, s_config.context);
        }
    }

    const int64_t long_press_us = (int64_t)s_config.long_press_ms * 1000;
    if (pressed && state->stable_pressed && !state->long_press_sent &&
        now_us - state->pressed_at_us >= long_press_us) {
        state->long_press_sent = true;
        s_config.handler(long_pressed_event, s_config.context);
    }
}

static void button_task(void *argument)
{
    (void)argument;

    const board_button_state_t initial = board_buttons_read();
    debounce_state_t boot = {
        .raw_pressed = initial.boot_pressed,
        .stable_pressed = initial.boot_pressed,
        .changed_at_us = esp_timer_get_time(),
        .pressed_at_us = esp_timer_get_time(),
    };
    debounce_state_t user = {
        .raw_pressed = initial.user_pressed,
        .stable_pressed = initial.user_pressed,
        .changed_at_us = esp_timer_get_time(),
        .pressed_at_us = esp_timer_get_time(),
    };

    TickType_t last_wake_time = xTaskGetTickCount();
    for (;;) {
        const board_button_state_t current = board_buttons_read();
        const int64_t now_us = esp_timer_get_time();
        update_button(
            &boot, current.boot_pressed,
            BUTTON_SERVICE_EVENT_BOOT_PRESSED,
            BUTTON_SERVICE_EVENT_BOOT_RELEASED,
            BUTTON_SERVICE_EVENT_BOOT_LONG_PRESSED,
            now_us);
        update_button(
            &user, current.user_pressed,
            BUTTON_SERVICE_EVENT_USER_PRESSED,
            BUTTON_SERVICE_EVENT_USER_RELEASED,
            BUTTON_SERVICE_EVENT_USER_LONG_PRESSED,
            now_us);
        vTaskDelayUntil(
            &last_wake_time, pdMS_TO_TICKS(s_config.poll_interval_ms));
    }
}

esp_err_t button_service_start(const button_service_config_t *config)
{
    if (config == NULL || config->handler == NULL ||
        config->poll_interval_ms == 0 || config->debounce_ms == 0 ||
        config->long_press_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = board_buttons_init();
    if (err != ESP_OK) {
        return err;
    }

    s_config = *config;
    if (xTaskCreate(button_task, "buttons", 2048, NULL, 6, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
