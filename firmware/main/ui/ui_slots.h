#pragma once

#include <lvgl.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_slots_create(lv_obj_t *parent);
void ui_slots_refresh(size_t terrarium_index);

#ifdef __cplusplus
}
#endif
