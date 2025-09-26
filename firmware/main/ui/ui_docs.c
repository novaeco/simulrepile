#include "ui_docs.h"

#include "doc_reader.h"
#include "i18n.h"
#include "ui_theme.h"

#include <stdlib.h>
#include <string.h>

#define UI_DOC_MAX_ENTRIES 64

static lv_obj_t *s_container;
static lv_obj_t *s_list;
static lv_obj_t *s_viewer;
static lv_obj_t *s_search;
static lv_obj_t *s_category;
static doc_entry_t s_entries[UI_DOC_MAX_ENTRIES];
static size_t s_entry_count = 0;
static doc_category_t s_current_category = DOC_CATEGORY_ALL;

static void update_viewer_text(const char *text)
{
    if (!s_viewer) {
        return;
    }
    lv_textarea_set_text(s_viewer, text ? text : "");
}

static void load_entry(size_t index)
{
    if (index >= s_entry_count) {
        return;
    }
    char *buffer = malloc(APP_DOC_READ_CHUNK_SIZE);
    if (!buffer) {
        return;
    }
    if (doc_reader_load(&s_entries[index], buffer, APP_DOC_READ_CHUNK_SIZE) >= 0) {
        update_viewer_text(buffer);
    }
    free(buffer);
}

static void list_event_cb(lv_event_t *e)
{
    size_t index = (size_t)lv_event_get_user_data(e);
    load_entry(index);
}

static void rebuild_list(const char *search)
{
    lv_obj_clean(s_list);
    if (search && strlen(search) > 0) {
        s_entry_count = doc_reader_search(search, s_entries, UI_DOC_MAX_ENTRIES);
    } else {
        s_entry_count = doc_reader_list(s_current_category, s_entries, UI_DOC_MAX_ENTRIES);
    }
    for (size_t i = 0; i < s_entry_count; ++i) {
        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, s_entries[i].name);
        lv_obj_add_event_cb(btn, list_event_cb, LV_EVENT_CLICKED, (void *)i);
    }
    if (s_entry_count > 0) {
        load_entry(0);
    } else {
        update_viewer_text(i18n_translate("docs.no_results"));
    }
}

static void category_changed(lv_event_t *e)
{
    (void)e;
    uint16_t selected = lv_dropdown_get_selected(s_category);
    switch (selected) {
    case 0:
        s_current_category = DOC_CATEGORY_ALL;
        break;
    case 1:
        s_current_category = DOC_CATEGORY_REGULATORY;
        break;
    case 2:
        s_current_category = DOC_CATEGORY_SPECIES;
        break;
    case 3:
        s_current_category = DOC_CATEGORY_GUIDES;
        break;
    default:
        s_current_category = DOC_CATEGORY_ALL;
        break;
    }
    rebuild_list(lv_textarea_get_text(s_search));
}

static void search_changed(lv_event_t *e)
{
    (void)e;
    rebuild_list(lv_textarea_get_text(s_search));
}

void ui_docs_init(lv_obj_t *parent)
{
    s_container = lv_obj_create(parent);
    lv_obj_set_size(s_container, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_container, LV_FLEX_FLOW_COLUMN);
    ui_theme_style_panel(s_container);

    lv_obj_t *toolbar = lv_obj_create(s_container);
    lv_obj_set_size(toolbar, LV_PCT(100), 60);
    lv_obj_set_style_pad_all(toolbar, 8, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    ui_theme_style_panel(toolbar);

    s_category = lv_dropdown_create(toolbar);
    lv_dropdown_set_options(s_category, "Tous\nRéglementaires\nEspèces\nGuides");
    lv_obj_add_event_cb(s_category, category_changed, LV_EVENT_VALUE_CHANGED, NULL);

    s_search = lv_textarea_create(toolbar);
    lv_obj_set_width(s_search, LV_PCT(60));
    lv_textarea_set_placeholder_text(s_search, i18n_translate("docs.search_placeholder"));
    lv_obj_add_event_cb(s_search, search_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *body = lv_obj_create(s_container);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    ui_theme_style_panel(body);

    s_list = lv_list_create(body);
    lv_obj_set_width(s_list, LV_PCT(35));
    lv_obj_set_height(s_list, LV_PCT(100));

    s_viewer = lv_textarea_create(body);
    lv_obj_set_width(s_viewer, LV_PCT(65));
    lv_obj_set_height(s_viewer, LV_PCT(100));
    lv_textarea_set_text(s_viewer, "");
    lv_textarea_set_one_line(s_viewer, false);
    lv_textarea_set_cursor_click_pos(s_viewer, false);
    lv_obj_add_flag(s_viewer, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_docs_show(void)
{
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    rebuild_list(lv_textarea_get_text(s_search));
}

lv_obj_t *ui_docs_container(void)
{
    return s_container;
}
