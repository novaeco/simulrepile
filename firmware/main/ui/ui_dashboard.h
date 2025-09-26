#pragma once

#include <lvgl.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_dashboard_init(lv_obj_t *parent);
void ui_dashboard_show(size_t terrarium_index);
void ui_dashboard_refresh(size_t terrarium_index);

#ifdef __cplusplus
}
#endif
