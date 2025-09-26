#include "ui_settings.h"

#include "app_config.h"
#include "i18n.h"
#include "log_manager.h"
#include "persist/save_manager.h"
#include "sim/sim_engine.h"
#include "ui_theme.h"

#include <stdio.h>

static lv_obj_t *s_container;
static lv_obj_t *s_log_text;

static void refresh_logs(void)
{
    if (!s_log_text) {
        return;
    }
    char buffer[512];
    size_t written = log_manager_copy_recent(buffer, sizeof(buffer));
    if (written == 0) {
        snprintf(buffer, sizeof(buffer), "%s", i18n_translate("settings.no_logs"));
    }
    lv_textarea_set_text(s_log_text, buffer);
}

static void language_changed(lv_event_t *e)
{
    (void)e;
    uint16_t index = lv_dropdown_get_selected(lv_event_get_target(e));
    app_lang_id_t lang = APP_LANG_ID_FR;
    switch (index) {
    case 0:
        lang = APP_LANG_ID_FR;
        break;
    case 1:
        lang = APP_LANG_ID_EN;
        break;
    case 2:
        lang = APP_LANG_ID_DE;
        break;
    case 3:
        lang = APP_LANG_ID_ES;
        break;
    default:
        lang = APP_DEFAULT_LANGUAGE;
        break;
    }
    i18n_set_language(lang);
}

static void contrast_changed(lv_event_t *e)
{
    bool enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    ui_theme_set_high_contrast(enabled);
    ui_theme_apply_root(lv_obj_get_parent(s_container));
}

static void save_now_cb(lv_event_t *e)
{
    (void)e;
    for (size_t i = 0; i < sim_engine_terrarium_count(); ++i) {
        const sim_terrarium_state_t *state = sim_engine_get_state(i);
        if (state) {
            save_manager_save_slot(i, state);
        }
    }
    log_manager_info("Manual save completed");
    refresh_logs();
}

static void flush_logs_cb(lv_event_t *e)
{
    (void)e;
    log_manager_flush_to_sd();
    refresh_logs();
}

void ui_settings_init(lv_obj_t *parent)
{
    s_container = lv_obj_create(parent);
    lv_obj_set_size(s_container, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_container, 12, 0);
    lv_obj_set_style_pad_gap(s_container, 10, 0);
    ui_theme_style_panel(s_container);

    lv_obj_t *lang_row = lv_obj_create(s_container);
    lv_obj_set_size(lang_row, LV_PCT(100), 70);
    lv_obj_set_flex_flow(lang_row, LV_FLEX_FLOW_ROW);
    ui_theme_style_panel(lang_row);
    lv_obj_t *lang_label = lv_label_create(lang_row);
    lv_label_set_text(lang_label, i18n_translate("settings.language"));
    lv_obj_t *dropdown = lv_dropdown_create(lang_row);
    lv_dropdown_set_options(dropdown, "Français\nEnglish\nDeutsch\nEspañol");
    lv_dropdown_set_selected(dropdown, (uint16_t)i18n_get_language());
    lv_obj_add_event_cb(dropdown, language_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *contrast_row = lv_obj_create(s_container);
    lv_obj_set_size(contrast_row, LV_PCT(100), 70);
    lv_obj_set_flex_flow(contrast_row, LV_FLEX_FLOW_ROW);
    ui_theme_style_panel(contrast_row);
    lv_obj_t *contrast_label = lv_label_create(contrast_row);
    lv_label_set_text(contrast_label, i18n_translate("settings.high_contrast"));
    lv_obj_t *contrast_switch = lv_switch_create(contrast_row);
    if (ui_theme_is_high_contrast()) {
        lv_obj_add_state(contrast_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(contrast_switch, contrast_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *actions_row = lv_obj_create(s_container);
    lv_obj_set_size(actions_row, LV_PCT(100), 80);
    lv_obj_set_flex_flow(actions_row, LV_FLEX_FLOW_ROW);
    ui_theme_style_panel(actions_row);
    lv_obj_t *save_btn = lv_btn_create(actions_row);
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, i18n_translate("settings.save_now"));
    lv_obj_add_event_cb(save_btn, save_now_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *flush_btn = lv_btn_create(actions_row);
    lv_obj_t *flush_label = lv_label_create(flush_btn);
    lv_label_set_text(flush_label, i18n_translate("settings.flush_logs"));
    lv_obj_add_event_cb(flush_btn, flush_logs_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *log_container = lv_obj_create(s_container);
    lv_obj_set_size(log_container, LV_PCT(100), LV_PCT(50));
    ui_theme_style_panel(log_container);
    s_log_text = lv_textarea_create(log_container);
    lv_obj_set_size(s_log_text, LV_PCT(100), LV_PCT(100));
    lv_textarea_set_one_line(s_log_text, false);
    lv_textarea_set_text(s_log_text, "");
}

void ui_settings_show(void)
{
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    refresh_logs();
}

lv_obj_t *ui_settings_container(void)
{
    return s_container;
}
