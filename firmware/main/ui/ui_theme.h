#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_theme_init(void);
void ui_theme_apply_root(lv_obj_t *root);
void ui_theme_style_panel(lv_obj_t *obj);
void ui_theme_set_high_contrast(bool enabled);
bool ui_theme_is_high_contrast(void);

#ifdef __cplusplus
}
#endif
