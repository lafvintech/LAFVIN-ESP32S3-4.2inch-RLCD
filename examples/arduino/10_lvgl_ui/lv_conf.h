/**
 * lv_conf.h -- Minimal LVGL 9.3 configuration for this course's monochrome display
 *
 * Usage (Arduino convention):
 *   Copy this file beside the lvgl directory, one level above the library:
 *     <sketchbook>/libraries/lv_conf.h
 *     <sketchbook>/libraries/lvgl/...
 *
 * Notes:
 *   Uses the official lv_conf_template.h (v9.3.0) configuration mechanism:
 *   only overridden macros are defined here; lv_conf_internal.h supplies the remaining defaults.
 *
 * Key differences from color-display configurations:
 *   LV_COLOR_DEPTH 1        -- Black-and-white 1bpp panel
 *   LV_USE_THEME_MONO       -- Monochrome theme (black-and-white outlines)
 *   Unneeded widgets are explicitly disabled; all other settings retain LVGL defaults.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* ---------- Color ---------- */
#define LV_COLOR_DEPTH 1

/* ---------- Memory ---------- */
#define LV_MEM_SIZE (64 * 1024U)

/* ---------- Tick ---------- */
/* The reflective always-on display needs no high frame rate; use a 100ms refresh period. */
#define LV_DEF_REFR_PERIOD 100

/* Disable LVGL's built-in OS integration; loop calls the handler. Arduino still runs on FreeRTOS. */
#define LV_USE_OS LV_OS_NONE

/* ---------- Fonts: keep only two sizes ---------- */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* ---------- Widget allowlist ---------- */
#define LV_USE_LABEL 1
#define LV_USE_LINE 0
#define LV_USE_BAR 1
#define LV_USE_BUTTON 1

/* Configure other widgets individually; retain Checkbox / Image / Slider / Switch for this example. */
#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGE 1
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 1
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 1
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

/* ---------- Theme: monochrome only ---------- */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 1

/* ---------- Layouts (small; retained for later use) ---------- */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/* ---------- Disable examples and demos ---------- */
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

/* ---------- Software rendering: RGB565 photos -> I1 monochrome output ---------- */
#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB565A8 0
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
// Retain ARGB8888 and I1 support for the monochrome theme and future indexed-image extensions.
#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_A8 0
#define LV_DRAW_SW_SUPPORT_I1 1

#endif /* LV_CONF_H */
