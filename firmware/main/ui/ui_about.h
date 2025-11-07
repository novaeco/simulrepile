#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_about_create(lv_obj_t *parent);
void ui_about_refresh_language(void);
void ui_about_update(void);

#ifdef __cplusplus
}
#endif
