#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_settings_create(lv_obj_t *parent);
void ui_settings_toggle_accessibility(bool enabled);
void ui_settings_set_language(uint16_t index);
void ui_settings_set_autosave_interval(uint32_t seconds);
void ui_settings_set_usb_mode(bool usb_enabled);
void ui_settings_on_profiles_reload(esp_err_t status, uint8_t terrarium_count);

#ifdef __cplusplus
}
#endif
