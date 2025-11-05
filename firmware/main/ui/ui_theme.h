#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_theme_apply_default(void);
void ui_theme_apply_high_contrast(bool enabled);
bool ui_theme_is_high_contrast(void);

void ui_theme_apply_screen_style(lv_obj_t *screen);
void ui_theme_apply_panel_style(lv_obj_t *panel);
void ui_theme_apply_label_style(lv_obj_t *label, bool accent);

#ifdef __cplusplus
}
#endif
