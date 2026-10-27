/* Demo10: app_main coordinates initialization/page switching; UI defines widgets; the port drives LVGL and refreshes independently.
  * Reading order: app_main -> ui -> rlcd_lvgl_port -> board_display.
  * No touch input: LVGL timers animate the widgets; KEY only switches pages. */
#include <stdint.h>

#include "board_display.h"
#include "button_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "rlcd_lvgl_port.h"
#include "ui.h"

static QueueHandle_t s_page_queue;

/* Runs in the button task: post press events only, without modifying LVGL or waiting for the display lock.
  * Drop extra page-switch requests if the queue is full rather than blocking button sampling. */
static void button_callback(button_service_event_t event, void *context)
{
    (void)context;
    if (event == BUTTON_SERVICE_EVENT_USER_PRESSED) {
        const uint8_t signal = 1;
        xQueueSend(s_page_queue, &signal, 0);
    }
}

void app_main(void)
{
    const board_display_config_t display_config = BOARD_DISPLAY_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(board_display_init(&display_config));
    ESP_ERROR_CHECK(rlcd_lvgl_port_start());
    rlcd_lvgl_port_lock(UINT32_MAX);
    demo_ui_build();
    rlcd_lvgl_port_unlock();

    s_page_queue = xQueueCreate(4, sizeof(uint8_t));
    ESP_ERROR_CHECK(s_page_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    const button_service_config_t button_config =
        BUTTON_SERVICE_CONFIG_DEFAULT(button_callback, NULL);
    ESP_ERROR_CHECK(button_service_start(&button_config));
    ESP_LOGI("lvgl_demo", "[10] KEY: switch widgets / photos");

    /* app_main runs alongside the LVGL task; application-side widget changes must use the same lock as the handler. */
    for (;;) {
        uint8_t signal;
        if (xQueueReceive(s_page_queue, &signal, portMAX_DELAY) == pdTRUE) {
            rlcd_lvgl_port_lock(UINT32_MAX);
            demo_ui_next_page();
            rlcd_lvgl_port_unlock();
        }
    }
}
