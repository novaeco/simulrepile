#include "ui/ui_about.h"

#include "bsp/waveshare_7b.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "i18n/i18n_manager.h"
#include "ui/ui_theme.h"

static const char *TAG = "ui_about";

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_title = NULL;
static lv_obj_t *s_description = NULL;
static lv_obj_t *s_legal = NULL;
static lv_obj_t *s_version = NULL;
static lv_obj_t *s_build = NULL;
static lv_obj_t *s_battery = NULL;

static void ui_about_update_version(void);

void ui_about_create(lv_obj_t *parent)
{
    if (!parent) {
        return;
    }

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(s_root, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_root, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ui_theme_apply_panel_style(s_root);

    s_title = lv_label_create(s_root);
    ui_theme_apply_label_style(s_title, true);

    s_description = lv_label_create(s_root);
    ui_theme_apply_label_style(s_description, false);
    lv_label_set_long_mode(s_description, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_description, LV_PCT(100));

    s_version = lv_label_create(s_root);
    ui_theme_apply_label_style(s_version, false);

    s_build = lv_label_create(s_root);
    ui_theme_apply_label_style(s_build, false);

    s_battery = lv_label_create(s_root);
    ui_theme_apply_label_style(s_battery, true);

    s_legal = lv_label_create(s_root);
    ui_theme_apply_label_style(s_legal, false);
    lv_label_set_long_mode(s_legal, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_legal, LV_PCT(100));

    ui_about_refresh_language();
    ui_about_update();
}

void ui_about_refresh_language(void)
{
    if (!s_root) {
        return;
    }

    const char *title = i18n_manager_get_string("about_title");
    if (title) {
        lv_label_set_text(s_title, title);
    }

    const char *description = i18n_manager_get_string("about_description");
    if (description) {
        lv_label_set_text(s_description, description);
    }

    const char *legal = i18n_manager_get_string("about_legal_text");
    if (legal) {
        lv_label_set_text(s_legal, legal);
    }

    ui_about_update_version();
}

static void ui_about_update_version(void)
{
    if (!s_version || !s_build) {
        return;
    }
    const esp_app_desc_t *desc = esp_app_get_description();
    if (!desc) {
        ESP_LOGW(TAG, "App description unavailable");
        return;
    }

    const char *version_fmt = i18n_manager_get_string("about_version_fmt");
    const char *build_fmt = i18n_manager_get_string("about_build_fmt");
    char buffer[128];

    if (version_fmt) {
        snprintf(buffer, sizeof(buffer), version_fmt, desc->version, desc->project_name);
        lv_label_set_text(s_version, buffer);
    }
    if (build_fmt) {
        snprintf(buffer, sizeof(buffer), build_fmt, desc->date, desc->time);
        lv_label_set_text(s_build, buffer);
    }
}

void ui_about_update(void)
{
    if (!s_battery) {
        return;
    }
    uint16_t mv = 0;
    esp_err_t err = bsp_battery_read_mv(&mv);
    if (err != ESP_OK) {
        const char *error_fmt = i18n_manager_get_string("about_battery_error_fmt");
        if (error_fmt) {
            char buffer[96];
            snprintf(buffer, sizeof(buffer), error_fmt, esp_err_to_name(err));
            lv_label_set_text(s_battery, buffer);
        }
        return;
    }

    const char *fmt = i18n_manager_get_string("about_battery_fmt");
    if (!fmt) {
        return;
    }
    char buffer[64];
    snprintf(buffer, sizeof(buffer), fmt, mv / 1000.0f);
    lv_label_set_text(s_battery, buffer);
}
