#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_theme_init(void);
void ui_theme_apply_root(lv_obj_t *root);
void ui_theme_style_panel(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif
