#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUTTON_SERVICE_EVENT_BOOT_PRESSED,
    BUTTON_SERVICE_EVENT_USER_PRESSED,
    BUTTON_SERVICE_EVENT_BOOT_RELEASED,
    BUTTON_SERVICE_EVENT_USER_RELEASED,
    BUTTON_SERVICE_EVENT_BOOT_LONG_PRESSED,
    BUTTON_SERVICE_EVENT_USER_LONG_PRESSED,
} button_service_event_t;

/* Callbacks run in the button-service task, not an ISR; post events quickly instead of refreshing, networking, or reading SD.
  * Press, release, and long press are separate events: applications needing only a click must explicitly filter PRESSED,
  * not treat every event other than USER_PRESSED as BOOT_PRESSED. */
typedef void (*button_service_handler_t)(
    button_service_event_t event, void *context);

typedef struct {
    uint32_t poll_interval_ms;
    uint32_t debounce_ms;
    uint32_t long_press_ms;
    button_service_handler_t handler;
    void *context;
} button_service_config_t;

#define BUTTON_SERVICE_CONFIG_DEFAULT(callback, callback_context) \
    {                                                               \
        .poll_interval_ms = 10,                                     \
        .debounce_ms = 30,                                          \
        .long_press_ms = 700,                                       \
        .handler = (callback),                                      \
        .context = (callback_context),                              \
    }

esp_err_t button_service_start(const button_service_config_t *config);

#ifdef __cplusplus
}
#endif
