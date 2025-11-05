#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_slots_create(lv_obj_t *parent);
void ui_slots_refresh(void);
uint32_t ui_slots_get_selection_mask(void);

#ifdef __cplusplus
}
#endif
