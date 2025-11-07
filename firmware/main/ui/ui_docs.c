#include "ui/ui_docs.h"

#include "esp_log.h"
#include "i18n/i18n_manager.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "docs/doc_reader.h"
#include "ui/ui_theme.h"
#include "esp_err.h"

#define UI_DOCS_MAX_ITEMS 24
#define UI_DOCS_BUFFER_SIZE 8192

typedef struct {
    const char *label_key;
    doc_category_t category;
} ui_docs_category_option_t;

static const char *TAG = "ui_docs";

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_header = NULL;
static lv_obj_t *s_header_title = NULL;
static lv_obj_t *s_category_dropdown = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_viewer = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_doc_buttons[UI_DOCS_MAX_ITEMS];
static doc_descriptor_t s_docs[UI_DOCS_MAX_ITEMS];
static int s_doc_count = 0;
static int s_selected_index = -1;
static doc_category_t s_current_category = DOC_CATEGORY_REGLEMENTAIRES;
static char s_doc_buffer[UI_DOCS_BUFFER_SIZE];
static bool s_events_suspended = false;
static char s_dropdown_buffer[128];

static const ui_docs_category_option_t s_category_options[] = {
    {.label_key = "docs_category_regulatory", .category = DOC_CATEGORY_REGLEMENTAIRES},
    {.label_key = "docs_category_species", .category = DOC_CATEGORY_SPECIES},
    {.label_key = "docs_category_guides", .category = DOC_CATEGORY_GUIDES},
};

static void ui_docs_build_layout(lv_obj_t *parent);
static void ui_docs_populate_category(doc_category_t category);
static void ui_docs_category_changed_cb(lv_event_t *event);
static void ui_docs_item_clicked_cb(lv_event_t *event);
static void ui_docs_update_selection(int index);
static bool ui_docs_is_html(const char *path);
static void ui_docs_sanitize_html(char *buffer);
static void ui_docs_update_dropdown(void);
static void ui_docs_update_header(void);

void ui_docs_create(lv_obj_t *parent)
{
    if (!parent) {
        return;
    }

    ESP_LOGI(TAG, "Creating document browser");
    ui_docs_build_layout(parent);
    ui_docs_refresh_language();
    ui_docs_populate_category(s_current_category);
}

void ui_docs_show_document(const char *path)
{
    if (!path) {
        return;
    }

    doc_descriptor_t descriptor = {
        .category = s_current_category,
        .path = path,
    };

    int out_len = 0;
    esp_err_t err = doc_reader_load(&descriptor, s_doc_buffer, sizeof(s_doc_buffer), &out_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load %s (%s)", path, esp_err_to_name(err));
        const char *text = i18n_manager_get_string("docs_viewer_error");
        if (!text) {
            text = "Unable to load document.";
        }
        lv_textarea_set_text(s_viewer, text);
        return;
    }

    if (ui_docs_is_html(path)) {
        ui_docs_sanitize_html(s_doc_buffer);
    }

    if (out_len >= (int)(sizeof(s_doc_buffer) - 1)) {
        const char *text = i18n_manager_get_string("docs_viewer_truncated");
        if (!text) {
            text = "Document truncated (maximum size reached).";
        }
        lv_textarea_set_text(s_viewer, text);
    } else {
        lv_textarea_set_text(s_viewer, s_doc_buffer);
    }
}

void ui_docs_refresh_category(void)
{
    ui_docs_populate_category(s_current_category);
}

void ui_docs_refresh_language(void)
{
    if (!s_root) {
        return;
    }
    ui_docs_update_header();
    ui_docs_update_dropdown();

    if (s_doc_count == 0 && s_status_label) {
        const char *loading = i18n_manager_get_string("docs_status_loading");
        if (!loading) {
            loading = "Loading...";
        }
        lv_label_set_text(s_status_label, loading);
    }

    if (s_viewer && s_selected_index < 0) {
        const char *placeholder = i18n_manager_get_string("docs_viewer_placeholder");
        if (!placeholder) {
            placeholder = "Select a document.";
        }
        lv_textarea_set_text(s_viewer, placeholder);
    }

    ui_docs_populate_category(s_current_category);
}

static void ui_docs_build_layout(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_root, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_root, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, LV_PART_MAIN);

    s_header = lv_obj_create(s_root);
    lv_obj_set_size(s_header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s_header, 12, LV_PART_MAIN);
    ui_theme_apply_panel_style(s_header);

    s_header_title = lv_label_create(s_header);
    ui_theme_apply_label_style(s_header_title, true);

    s_category_dropdown = lv_dropdown_create(s_header);
    lv_obj_add_event_cb(s_category_dropdown, ui_docs_category_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *body = lv_obj_create(s_root);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(body, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_column(body, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);

    s_list = lv_list_create(body);
    lv_obj_set_width(s_list, LV_PCT(32));
    lv_obj_set_flex_grow(s_list, 0);
    lv_obj_set_style_pad_all(s_list, 8, LV_PART_MAIN);
    ui_theme_apply_panel_style(s_list);

    s_status_label = lv_label_create(s_list);
    ui_theme_apply_label_style(s_status_label, false);

    s_viewer = lv_textarea_create(body);
    lv_obj_set_flex_grow(s_viewer, 1);
    lv_obj_set_style_pad_all(s_viewer, 16, LV_PART_MAIN);
    lv_textarea_set_one_line(s_viewer, false);
    lv_textarea_set_wrap_mode(s_viewer, LV_TEXTAREA_WRAP_WORD);
    lv_textarea_set_password_mode(s_viewer, false);
    lv_textarea_set_cursor_click_pos(s_viewer, false);
    lv_textarea_set_scroll_dir(s_viewer, LV_DIR_VER);
    ui_theme_apply_panel_style(s_viewer);
    ui_theme_apply_label_style(lv_textarea_get_label(s_viewer), false);
    lv_textarea_set_text(s_viewer, "");
}

static void ui_docs_populate_category(doc_category_t category)
{
    if (!s_list) {
        return;
    }

    s_events_suspended = true;

    for (int i = 0; i < s_doc_count; ++i) {
        s_doc_buttons[i] = NULL;
    }
    s_doc_count = 0;
    s_selected_index = -1;

    lv_obj_clean(s_list);
    s_status_label = lv_label_create(s_list);
    ui_theme_apply_label_style(s_status_label, false);
    const char *loading = i18n_manager_get_string("docs_status_loading");
    if (!loading) {
        loading = "Loading...";
    }
    lv_label_set_text(s_status_label, loading);

    int count = 0;
    esp_err_t err = doc_reader_list(category, s_docs, UI_DOCS_MAX_ITEMS, &count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to list docs for category %d (%s)", category, esp_err_to_name(err));
        const char *fmt = i18n_manager_get_string("docs_status_error_fmt");
        if (!fmt) {
            fmt = "Unable to list documents (%s)";
        }
        lv_label_set_text_fmt(s_status_label, fmt, esp_err_to_name(err));
        s_events_suspended = false;
        return;
    }

    s_doc_count = count;
    if (s_doc_count == 0) {
        const char *empty = i18n_manager_get_string("docs_status_empty");
        if (!empty) {
            empty = "No documents available";
        }
        lv_label_set_text(s_status_label, empty);
        s_events_suspended = false;
        const char *viewer_empty = i18n_manager_get_string("docs_viewer_empty");
        if (!viewer_empty) {
            viewer_empty = "This category has no documents.";
        }
        lv_textarea_set_text(s_viewer, viewer_empty);
        return;
    }

    lv_obj_del(s_status_label);
    s_status_label = NULL;

    for (int i = 0; i < s_doc_count; ++i) {
        lv_obj_t *btn = lv_list_add_btn(s_list, LV_SYMBOL_FILE, s_docs[i].path);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_add_event_cb(btn, ui_docs_item_clicked_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        s_doc_buttons[i] = btn;
    }

    s_events_suspended = false;
    ui_docs_update_selection(0);
}

static void ui_docs_category_changed_cb(lv_event_t *event)
{
    if (!event) {
        return;
    }
    if (s_events_suspended) {
        return;
    }

    uint16_t selected = lv_dropdown_get_selected(s_category_dropdown);
    if (selected >= (sizeof(s_category_options) / sizeof(s_category_options[0]))) {
        selected = 0;
    }

    s_current_category = s_category_options[selected].category;
    ui_docs_populate_category(s_current_category);
}

static void ui_docs_item_clicked_cb(lv_event_t *event)
{
    if (!event || s_events_suspended) {
        return;
    }

    int index = (int)(uintptr_t)lv_event_get_user_data(event);
    ui_docs_update_selection(index);
}

static void ui_docs_update_selection(int index)
{
    if (index < 0 || index >= s_doc_count) {
        return;
    }

    s_events_suspended = true;
    for (int i = 0; i < s_doc_count; ++i) {
        if (!s_doc_buttons[i]) {
            continue;
        }
        if (i == index) {
            lv_obj_add_state(s_doc_buttons[i], LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_doc_buttons[i], LV_STATE_CHECKED);
        }
    }
    s_events_suspended = false;

    s_selected_index = index;
    ui_docs_show_document(s_docs[index].path);
}

static bool ui_docs_is_html(const char *path)
{
    if (!path) {
        return false;
    }
    const char *dot = strrchr(path, '.');
    if (!dot) {
        return false;
    }
    ++dot;
    char ext[5] = {0};
    size_t i = 0;
    while (dot[i] != '\0' && i < sizeof(ext) - 1) {
        ext[i] = (char)tolower((unsigned char)dot[i]);
        ++i;
    }
    return (strcmp(ext, "html") == 0) || (strcmp(ext, "htm") == 0);
}

static void ui_docs_sanitize_html(char *buffer)
{
    if (!buffer) {
        return;
    }

    bool in_tag = false;
    char *src = buffer;
    char *dst = buffer;

    while (*src) {
        if (*src == '<') {
            in_tag = true;
            ++src;
            continue;
        }
        if (*src == '>') {
            in_tag = false;
            ++src;
            continue;
        }
        if (!in_tag) {
            *dst++ = *src;
        }
        ++src;
    }
    *dst = '\0';
}

static void ui_docs_update_dropdown(void)
{
    if (!s_category_dropdown) {
        return;
    }
    s_dropdown_buffer[0] = '\0';
    size_t offset = 0;
    size_t remaining = sizeof(s_dropdown_buffer);
    for (size_t i = 0; i < (sizeof(s_category_options) / sizeof(s_category_options[0])); ++i) {
        const char *label = i18n_manager_get_string(s_category_options[i].label_key);
        if (!label) {
            label = "?";
        }
        size_t needed = strlen(label) + 1;
        if (needed + 1 > remaining) {
            break;
        }
        strcpy(&s_dropdown_buffer[offset], label);
        offset += strlen(label);
        remaining = sizeof(s_dropdown_buffer) - offset;
        if (i + 1 < (sizeof(s_category_options) / sizeof(s_category_options[0]))) {
            s_dropdown_buffer[offset++] = '\n';
            remaining = sizeof(s_dropdown_buffer) - offset;
        }
    }
    s_dropdown_buffer[offset] = '\0';
    lv_dropdown_set_options(s_category_dropdown, s_dropdown_buffer);

    for (uint16_t i = 0; i < (uint16_t)(sizeof(s_category_options) / sizeof(s_category_options[0])); ++i) {
        if (s_category_options[i].category == s_current_category) {
            lv_dropdown_set_selected(s_category_dropdown, i);
            break;
        }
    }
}

static void ui_docs_update_header(void)
{
    if (!s_header_title) {
        return;
    }
    const char *title = i18n_manager_get_string("docs_title");
    if (!title) {
        title = "Document library";
    }
    lv_label_set_text(s_header_title, title);
}
