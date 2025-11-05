#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_settings_create(lv_obj_t *parent);
void ui_settings_toggle_accessibility(bool enabled);
void ui_settings_set_language(uint16_t index);
void ui_settings_set_autosave_interval(uint32_t seconds);
void ui_settings_set_usb_mode(bool usb_enabled);

#ifdef __cplusplus
}
#endif
