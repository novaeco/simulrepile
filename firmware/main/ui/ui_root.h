#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_root_init(lv_disp_t *disp);
void ui_root_show_dashboard(size_t terrarium_index);
void ui_root_show_documents(void);
void ui_root_show_settings(void);
void ui_root_update_terrarium(size_t index);

#ifdef __cplusplus
}
#endif
