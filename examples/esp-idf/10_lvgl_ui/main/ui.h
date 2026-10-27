#pragma once

/* Both interfaces require the caller to hold the rlcd_lvgl_port lock.
  * Call build once after startup; next_page then reuses the widget tree without recreating it. */
void demo_ui_build(void);
void demo_ui_next_page(void);
