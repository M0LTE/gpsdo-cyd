// LVGL configuration shared by the CYD firmware and the PC simulator.
// Anything not set here takes LVGL's default from lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
// System malloc: on the ESP32 a large static LVGL pool would eat the limited static DRAM.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_DEF_REFR_PERIOD 33
#define LV_DPI_DEF 130
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_CHART 1
#define LV_USE_CANVAS 1
#define LV_USE_THEME_DEFAULT 0
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#ifdef ARDUINO
#define LV_USE_TFT_ESPI 1
#endif

#endif
