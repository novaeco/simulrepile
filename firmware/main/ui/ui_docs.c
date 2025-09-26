#include "ui_docs.h"

#include "doc_reader.h"
#include "i18n.h"
#include "ui_theme.h"

#include <stdio.h>

static lv_obj_t *s_container;
static lv_obj_t *s_text_area;

static void refresh_document_list(void)
{
    char buffer[128];
    size_t count = doc_reader_list_documents(buffer, sizeof(buffer));
    lv_textarea_set_text(s_text_area, buffer);
}

void ui_docs_init(lv_obj_t *parent)
{
    s_container = lv_obj_create(parent);
    lv_obj_set_size(s_container, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    ui_theme_style_panel(s_container);

    s_text_area = lv_textarea_create(s_container);
    lv_obj_set_size(s_text_area, LV_PCT(95), LV_PCT(95));
    lv_obj_align(s_text_area, LV_ALIGN_CENTER, 0, 0);
    lv_textarea_set_text(s_text_area, "");
}

void ui_docs_show(void)
{
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    refresh_document_list();
}
