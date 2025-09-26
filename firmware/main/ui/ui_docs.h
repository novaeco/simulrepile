#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_docs_init(lv_obj_t *parent);
void ui_docs_show(void);
lv_obj_t *ui_docs_container(void);

#ifdef __cplusplus
}
#endif
