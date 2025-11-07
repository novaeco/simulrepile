#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_docs_create(lv_obj_t *parent);
void ui_docs_show_document(const char *path);
void ui_docs_refresh_category(void);
void ui_docs_refresh_language(void);

#ifdef __cplusplus
}
#endif
