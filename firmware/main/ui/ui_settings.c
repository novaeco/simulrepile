#include "ui/ui_settings.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/exio.h"
#include "esp_err.h"
#include "i18n/i18n_manager.h"
#include "link/core_link.h"
#include "lvgl_port.h"
#include "persist/save_service.h"
#include "tts/tts_stub.h"
#include "ui/ui_root.h"
#include "ui/ui_theme.h"
#include "updates/updates_manager.h"

typedef struct {
    const char *label_key;
    const char *code;
    i18n_language_t language;
} ui_settings_language_option_t;

static const ui_settings_language_option_t s_language_options[] = {
    {.label_key = "settings_language_option_fr", .code = "fr", .language = I18N_LANG_FR},
    {.label_key = "settings_language_option_en", .code = "en", .language = I18N_LANG_EN},
    {.label_key = "settings_language_option_de", .code = "de", .language = I18N_LANG_DE},
    {.label_key = "settings_language_option_es", .code = "es", .language = I18N_LANG_ES},
};

typedef enum {
    UI_SETTINGS_UPDATE_STATUS_IDLE = 0,
    UI_SETTINGS_UPDATE_STATUS_AVAILABLE,
    UI_SETTINGS_UPDATE_STATUS_NONE,
    UI_SETTINGS_UPDATE_STATUS_ERROR,
    UI_SETTINGS_UPDATE_STATUS_APPLIED,
} ui_settings_update_status_t;

static const char *TAG = "ui_settings";
static char s_language_options_buffer[256];

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_language_dropdown = NULL;
static lv_obj_t *s_contrast_switch = NULL;
static lv_obj_t *s_autosave_slider = NULL;
static lv_obj_t *s_autosave_label = NULL;
static lv_obj_t *s_usb_switch = NULL;
static lv_obj_t *s_update_last_flash_label = NULL;
static lv_obj_t *s_update_status_label = NULL;
static lv_obj_t *s_update_check_btn = NULL;
static lv_obj_t *s_update_apply_btn = NULL;
static lv_obj_t *s_profiles_status_label = NULL;
static lv_obj_t *s_language_label = NULL;
static lv_obj_t *s_contrast_label = NULL;
static lv_obj_t *s_autosave_title = NULL;
static lv_obj_t *s_profiles_label = NULL;
static lv_obj_t *s_profiles_button_label = NULL;
static lv_obj_t *s_usb_label = NULL;
static lv_obj_t *s_updates_title = NULL;
static lv_obj_t *s_update_check_label = NULL;
static lv_obj_t *s_update_apply_label = NULL;
#if CONFIG_APP_ENABLE_TTS_STUB
static lv_obj_t *s_tts_label = NULL;
static lv_obj_t *s_tts_description = NULL;
static lv_obj_t *s_tts_switch = NULL;
#endif

static uint32_t s_autosave_interval_s = CONFIG_APP_AUTOSAVE_INTERVAL_S;
static bool s_usb_selected = true;
static bool s_events_suspended = false;
static bool s_update_available = false;
static updates_manifest_info_t s_update_info;
static ui_settings_update_status_t s_update_state = UI_SETTINGS_UPDATE_STATUS_IDLE;
static esp_err_t s_update_last_error = ESP_OK;
static bool s_profiles_status_initialized = false;
static bool s_profiles_pending = false;
static esp_err_t s_profiles_last_status = ESP_OK;
static uint8_t s_profiles_last_count = 0;

static void ui_settings_build_layout(lv_obj_t *parent);
static void ui_settings_update_autosave_label(void);
static void ui_settings_update_language_options(void);
static void ui_settings_language_changed_cb(lv_event_t *event);
static void ui_settings_contrast_changed_cb(lv_event_t *event);
static void ui_settings_autosave_changed_cb(lv_event_t *event);
static void ui_settings_usb_changed_cb(lv_event_t *event);
static void ui_settings_updates_refresh(void);
static void ui_settings_updates_refresh_last_flash(void);
static void ui_settings_update_update_status_label(void);
static void ui_settings_updates_check_cb(lv_event_t *event);
static void ui_settings_updates_apply_cb(lv_event_t *event);
static void ui_settings_profiles_reload_cb(lv_event_t *event);
static void ui_settings_update_profiles_status(void);
#if CONFIG_APP_ENABLE_TTS_STUB
static void ui_settings_tts_changed_cb(lv_event_t *event);
static void ui_settings_update_tts_state(void);
#endif

void ui_settings_create(lv_obj_t *parent)
{
    if (!parent) {
        return;
    }

    ESP_LOGI(TAG, "Creating settings view");
    ui_settings_build_layout(parent);
    ui_settings_refresh_language();
    ui_settings_toggle_accessibility(CONFIG_APP_THEME_HIGH_CONTRAST);

    uint16_t default_index = 0;
    for (uint16_t i = 0; i < (uint16_t)(sizeof(s_language_options) / sizeof(s_language_options[0])); ++i) {
        if (strcmp(s_language_options[i].code, CONFIG_APP_LANG_DEFAULT) == 0) {
            default_index = i;
            break;
        }
    }

    ui_settings_set_language(default_index);
    ui_settings_set_autosave_interval(s_autosave_interval_s);
    ui_settings_set_usb_mode(true);
    ui_settings_updates_refresh();
    ui_settings_update_profiles_status();
#if CONFIG_APP_ENABLE_TTS_STUB
    ui_settings_update_tts_state();
#endif
}

void ui_settings_toggle_accessibility(bool enabled)
{
    ESP_LOGI(TAG, "Accessibility mode %s", enabled ? "enabled" : "disabled");
    ui_theme_apply_high_contrast(enabled);

    if (!s_contrast_switch) {
        return;
    }

    s_events_suspended = true;
    if (enabled) {
        lv_obj_add_state(s_contrast_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_contrast_switch, LV_STATE_CHECKED);
    }
    s_events_suspended = false;
}

void ui_settings_set_language(uint16_t index)
{
    if (!s_language_dropdown) {
        return;
    }

    if (index >= (sizeof(s_language_options) / sizeof(s_language_options[0]))) {
        index = 0;
    }

    s_events_suspended = true;
    lv_dropdown_set_selected(s_language_dropdown, index);
    s_events_suspended = false;

    esp_err_t err = i18n_manager_set_language(s_language_options[index].language);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set language %s: %s", s_language_options[index].code, esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Language switched to %s", s_language_options[index].code);
    save_service_notify_language_changed();
    ui_root_refresh_language();
}

void ui_settings_set_autosave_interval(uint32_t seconds)
{
    if (seconds < 30U) {
        seconds = 30U;
    }
    if (seconds > 3600U) {
        seconds = 3600U;
    }

    s_autosave_interval_s = seconds;

    if (s_autosave_slider) {
        s_events_suspended = true;
        lv_slider_set_range(s_autosave_slider, 30, 3600);
        lv_slider_set_value(s_autosave_slider, (int32_t)seconds, LV_ANIM_OFF);
        s_events_suspended = false;
    }

    ui_settings_update_autosave_label();
    esp_err_t err = save_service_set_interval(seconds);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update autosave service interval: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Autosave interval set to %u s", (unsigned)seconds);
    }
}

void ui_settings_set_usb_mode(bool usb_enabled)
{
#if CONFIG_BSP_USB_CAN_SELECTABLE
    s_usb_selected = usb_enabled;
    if (s_usb_switch) {
        s_events_suspended = true;
        if (usb_enabled) {
            lv_obj_add_state(s_usb_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_usb_switch, LV_STATE_CHECKED);
        }
        s_events_suspended = false;
    }
    esp_err_t err = exio_select_usb(usb_enabled);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to switch interface: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Interface switched to %s", usb_enabled ? "USB" : "CAN");
    }
#else
    (void)usb_enabled;
#endif
}

static void ui_settings_build_layout(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_root, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_root, 20, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t *language_card = lv_obj_create(s_root);
    ui_theme_apply_panel_style(language_card);
    lv_obj_set_size(language_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(language_card, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(language_card, LV_FLEX_FLOW_COLUMN);

    s_language_label = lv_label_create(language_card);
    ui_theme_apply_label_style(s_language_label, true);

    s_language_dropdown = lv_dropdown_create(language_card);
    lv_obj_add_event_cb(s_language_dropdown, ui_settings_language_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *contrast_card = lv_obj_create(s_root);
    ui_theme_apply_panel_style(contrast_card);
    lv_obj_set_size(contrast_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(contrast_card, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(contrast_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(contrast_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_contrast_label = lv_label_create(contrast_card);
    ui_theme_apply_label_style(s_contrast_label, true);

    s_contrast_switch = lv_switch_create(contrast_card);
    lv_obj_add_event_cb(s_contrast_switch, ui_settings_contrast_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *autosave_card = lv_obj_create(s_root);
    ui_theme_apply_panel_style(autosave_card);
    lv_obj_set_size(autosave_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(autosave_card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(autosave_card, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(autosave_card, LV_FLEX_FLOW_COLUMN);

    s_autosave_title = lv_label_create(autosave_card);
    ui_theme_apply_label_style(s_autosave_title, true);

    s_autosave_slider = lv_slider_create(autosave_card);
    lv_slider_set_range(s_autosave_slider, 30, 3600);
    lv_obj_add_event_cb(s_autosave_slider, ui_settings_autosave_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_autosave_label = lv_label_create(autosave_card);
    ui_theme_apply_label_style(s_autosave_label, false);

    lv_obj_t *profiles_card = lv_obj_create(s_root);
    ui_theme_apply_panel_style(profiles_card);
    lv_obj_set_size(profiles_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(profiles_card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(profiles_card, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(profiles_card, LV_FLEX_FLOW_COLUMN);

    s_profiles_label = lv_label_create(profiles_card);
    ui_theme_apply_label_style(s_profiles_label, true);

    lv_obj_t *profiles_reload_btn = lv_button_create(profiles_card);
    lv_obj_set_size(profiles_reload_btn, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(profiles_reload_btn, ui_settings_profiles_reload_cb, LV_EVENT_CLICKED, NULL);
    ui_theme_apply_panel_style(profiles_reload_btn);

    s_profiles_button_label = lv_label_create(profiles_reload_btn);
    ui_theme_apply_label_style(s_profiles_button_label, true);
    lv_obj_center(s_profiles_button_label);

    s_profiles_status_label = lv_label_create(profiles_card);
    ui_theme_apply_label_style(s_profiles_status_label, false);
    lv_label_set_long_mode(s_profiles_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_profiles_status_label, LV_PCT(100));

    lv_obj_t *usb_card = lv_obj_create(s_root);
    ui_theme_apply_panel_style(usb_card);
    lv_obj_set_size(usb_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(usb_card, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(usb_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(usb_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_usb_label = lv_label_create(usb_card);
    ui_theme_apply_label_style(s_usb_label, true);

    s_usb_switch = lv_switch_create(usb_card);
    lv_obj_add_event_cb(s_usb_switch, ui_settings_usb_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
#if !CONFIG_BSP_USB_CAN_SELECTABLE
    lv_obj_add_state(s_usb_switch, LV_STATE_DISABLED);
#endif

#if CONFIG_APP_ENABLE_TTS_STUB
    lv_obj_t *tts_card = lv_obj_create(s_root);
    ui_theme_apply_panel_style(tts_card);
    lv_obj_set_size(tts_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(tts_card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(tts_card, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(tts_card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *tts_header = lv_obj_create(tts_card);
    lv_obj_set_size(tts_header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(tts_header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(tts_header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tts_header, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(tts_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tts_header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_tts_label = lv_label_create(tts_header);
    ui_theme_apply_label_style(s_tts_label, true);

    s_tts_switch = lv_switch_create(tts_header);
    lv_obj_add_event_cb(s_tts_switch, ui_settings_tts_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_tts_description = lv_label_create(tts_card);
    ui_theme_apply_label_style(s_tts_description, false);
    lv_label_set_long_mode(s_tts_description, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_tts_description, LV_PCT(100));
#endif

    lv_obj_t *updates_card = lv_obj_create(s_root);
    ui_theme_apply_panel_style(updates_card);
    lv_obj_set_size(updates_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(updates_card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(updates_card, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(updates_card, LV_FLEX_FLOW_COLUMN);

    s_updates_title = lv_label_create(updates_card);
    ui_theme_apply_label_style(s_updates_title, true);

    s_update_last_flash_label = lv_label_create(updates_card);
    ui_theme_apply_label_style(s_update_last_flash_label, false);
    lv_label_set_long_mode(s_update_last_flash_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_update_last_flash_label, LV_PCT(100));

    s_update_status_label = lv_label_create(updates_card);
    ui_theme_apply_label_style(s_update_status_label, false);
    lv_label_set_long_mode(s_update_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_update_status_label, LV_PCT(100));

    lv_obj_t *updates_btn_row = lv_obj_create(updates_card);
    lv_obj_set_size(updates_btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(updates_btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(updates_btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(updates_btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(updates_btn_row, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(updates_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(updates_btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_update_check_btn = lv_button_create(updates_btn_row);
    ui_theme_apply_panel_style(s_update_check_btn);
    lv_obj_add_event_cb(s_update_check_btn, ui_settings_updates_check_cb, LV_EVENT_CLICKED, NULL);

    s_update_check_label = lv_label_create(s_update_check_btn);
    ui_theme_apply_label_style(s_update_check_label, true);
    lv_obj_center(s_update_check_label);

    s_update_apply_btn = lv_button_create(updates_btn_row);
    ui_theme_apply_panel_style(s_update_apply_btn);
    lv_obj_add_event_cb(s_update_apply_btn, ui_settings_updates_apply_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(s_update_apply_btn, LV_STATE_DISABLED);

    s_update_apply_label = lv_label_create(s_update_apply_btn);
    ui_theme_apply_label_style(s_update_apply_label, true);
    lv_obj_center(s_update_apply_label);
}

static void ui_settings_update_autosave_label(void)
{
    if (!s_autosave_label) {
        return;
    }
    const char *fmt = i18n_manager_get_string("settings_autosave_value_fmt");
    if (!fmt) {
        fmt = "Interval: %u s";
    }
    lv_label_set_text_fmt(s_autosave_label, fmt, (unsigned)s_autosave_interval_s);
}

static void ui_settings_language_changed_cb(lv_event_t *event)
{
    if (s_events_suspended || !event) {
        return;
    }
    uint16_t selected = lv_dropdown_get_selected(s_language_dropdown);
    ui_settings_set_language(selected);
}

static void ui_settings_contrast_changed_cb(lv_event_t *event)
{
    if (s_events_suspended || !event) {
        return;
    }
    bool enabled = lv_obj_has_state(s_contrast_switch, LV_STATE_CHECKED);
    ui_settings_toggle_accessibility(enabled);
}

static void ui_settings_autosave_changed_cb(lv_event_t *event)
{
    if (s_events_suspended || !event) {
        return;
    }
    int32_t value = lv_slider_get_value(s_autosave_slider);
    ui_settings_set_autosave_interval((uint32_t)value);
}

static void ui_settings_usb_changed_cb(lv_event_t *event)
{
    if (s_events_suspended || !event) {
        return;
    }
    bool usb_enabled = lv_obj_has_state(s_usb_switch, LV_STATE_CHECKED);
    if (usb_enabled == s_usb_selected) {
        return;
    }
    ui_settings_set_usb_mode(usb_enabled);
}

static void ui_settings_updates_refresh_last_flash(void)
{
    if (!s_update_last_flash_label) {
        return;
    }

    updates_flash_report_t report;
    esp_err_t err = updates_get_last_flash_report(&report);
    if (err == ESP_ERR_NOT_FOUND) {
        const char *text = i18n_manager_get_string("settings_updates_last_flash_none");
        if (!text) {
            text = "Last flash: no history";
        }
        lv_label_set_text(s_update_last_flash_label, text);
        return;
    }
    if (err != ESP_OK) {
        const char *fmt = i18n_manager_get_string("settings_updates_last_flash_error_fmt");
        if (!fmt) {
            fmt = "Last flash: error (%s)";
        }
        lv_label_set_text_fmt(s_update_last_flash_label, fmt, esp_err_to_name(err));
        return;
    }

    const char *version = report.manifest.version[0] ? report.manifest.version : "?";
    const char *partition = report.partition_label[0] ? report.partition_label : "?";
    const char *error_name = esp_err_to_name(report.error);

    switch (report.outcome) {
        case UPDATES_FLASH_OUTCOME_SUCCESS: {
            const char *fmt = i18n_manager_get_string("settings_updates_last_flash_success_fmt");
            if (!fmt) {
                fmt = "Last flash: success v%s → %s";
            }
            lv_label_set_text_fmt(s_update_last_flash_label, fmt, version, partition);
            break;
        }
        case UPDATES_FLASH_OUTCOME_ERROR: {
            const char *fmt = i18n_manager_get_string("settings_updates_last_flash_error_fmt");
            if (!fmt) {
                fmt = "Last flash: error (%s)";
            }
            lv_label_set_text_fmt(s_update_last_flash_label, fmt, error_name);
            break;
        }
        case UPDATES_FLASH_OUTCOME_ROLLBACK: {
            const char *fmt = i18n_manager_get_string("settings_updates_last_flash_rollback_fmt");
            if (!fmt) {
                fmt = "Last flash: rollback %s (%s)";
            }
            lv_label_set_text_fmt(s_update_last_flash_label, fmt, partition, error_name);
            break;
        }
        case UPDATES_FLASH_OUTCOME_NONE:
        default: {
            const char *text = i18n_manager_get_string("settings_updates_last_flash_unknown");
            if (!text) {
                text = "Last flash: unknown";
            }
            lv_label_set_text(s_update_last_flash_label, text);
            break;
        }
    }
}

static void ui_settings_updates_refresh(void)
{
    updates_manifest_info_t info;
    esp_err_t err = updates_check_available(&info);
    if (err == ESP_OK) {
        s_update_available = true;
        s_update_info = info;
        s_update_state = UI_SETTINGS_UPDATE_STATUS_AVAILABLE;
        s_update_last_error = ESP_OK;
    } else if (err == ESP_ERR_NOT_FOUND) {
        s_update_available = false;
        s_update_state = UI_SETTINGS_UPDATE_STATUS_NONE;
        s_update_last_error = ESP_OK;
    } else {
        s_update_available = false;
        s_update_state = UI_SETTINGS_UPDATE_STATUS_ERROR;
        s_update_last_error = err;
    }

    ui_settings_update_update_status_label();
    ui_settings_updates_refresh_last_flash();
}

static void ui_settings_updates_check_cb(lv_event_t *event)
{
    if (s_events_suspended || !event) {
        return;
    }
    ui_settings_updates_refresh();
}

static void ui_settings_updates_apply_cb(lv_event_t *event)
{
    if (s_events_suspended || !event) {
        return;
    }
    if (!s_update_available) {
        ui_settings_updates_refresh();
        return;
    }

    s_events_suspended = true;
    if (s_update_apply_btn) {
        lv_obj_add_state(s_update_apply_btn, LV_STATE_DISABLED);
    }

    esp_err_t err = updates_apply(&s_update_info);
    if (err == ESP_OK) {
        s_update_available = false;
        s_update_state = UI_SETTINGS_UPDATE_STATUS_APPLIED;
        s_update_last_error = ESP_OK;
    } else {
        s_update_available = false;
        s_update_state = UI_SETTINGS_UPDATE_STATUS_ERROR;
        s_update_last_error = err;
    }

    s_events_suspended = false;
    ui_settings_update_update_status_label();
    ui_settings_updates_refresh_last_flash();
}

static void ui_settings_profiles_reload_cb(lv_event_t *event)
{
    (void)event;
    if (!s_profiles_status_label) {
        return;
    }

    esp_err_t err = core_link_request_profile_reload(NULL);
    if (err == ESP_OK) {
        s_profiles_pending = true;
        s_profiles_status_initialized = false;
        const char *text = i18n_manager_get_string("settings_profiles_reload_request");
        if (!text) {
            text = "Reload requested...";
        }
        lv_label_set_text(s_profiles_status_label, text);
    } else {
        s_profiles_pending = false;
        s_profiles_status_initialized = true;
        s_profiles_last_status = err;
        s_profiles_last_count = 0;
        ui_settings_update_profiles_status();
        ESP_LOGW(TAG, "Profile reload request failed: %s", esp_err_to_name(err));
    }
}

void ui_settings_on_profiles_reload(esp_err_t status, uint8_t terrarium_count)
{
    s_profiles_pending = false;
    s_profiles_status_initialized = true;
    s_profiles_last_status = status;
    s_profiles_last_count = terrarium_count;

    lvgl_port_lock();
    ui_settings_update_profiles_status();
    lvgl_port_unlock();
}

void ui_settings_refresh_language(void)
{
    if (!s_root) {
        return;
    }

    s_events_suspended = true;

    if (s_language_label) {
        const char *text = i18n_manager_get_string("settings_language_title");
        if (!text) {
            text = "Language";
        }
        lv_label_set_text(s_language_label, text);
    }
    ui_settings_update_language_options();

    if (s_contrast_label) {
        const char *text = i18n_manager_get_string("settings_contrast_title");
        if (!text) {
            text = "High contrast theme";
        }
        lv_label_set_text(s_contrast_label, text);
    }

    if (s_autosave_title) {
        const char *text = i18n_manager_get_string("settings_autosave_title");
        if (!text) {
            text = "Autosave interval";
        }
        lv_label_set_text(s_autosave_title, text);
    }
    ui_settings_update_autosave_label();

    if (s_profiles_label) {
        const char *text = i18n_manager_get_string("settings_profiles_title");
        if (!text) {
            text = "Terrarium profiles";
        }
        lv_label_set_text(s_profiles_label, text);
    }
    if (s_profiles_button_label) {
        const char *text = i18n_manager_get_string("settings_profiles_reload");
        if (!text) {
            text = "Reload from SD";
        }
        lv_label_set_text(s_profiles_button_label, text);
    }

    if (s_usb_label) {
        const char *text = i18n_manager_get_string("settings_usb_title");
        if (!text) {
            text = "USB ↔ CAN selector";
        }
        lv_label_set_text(s_usb_label, text);
    }

#if CONFIG_APP_ENABLE_TTS_STUB
    if (s_tts_label) {
        const char *text = i18n_manager_get_string("settings_tts_title");
        if (!text) {
            text = "Text-to-Speech";
        }
        lv_label_set_text(s_tts_label, text);
    }
    if (s_tts_description) {
        const char *text = i18n_manager_get_string("settings_tts_description");
        if (!text) {
            text = "Enable spoken alerts and confirmations.";
        }
        lv_label_set_text(s_tts_description, text);
    }
#endif

    if (s_updates_title) {
        const char *text = i18n_manager_get_string("settings_updates_title");
        if (!text) {
            text = "Updates via SD";
        }
        lv_label_set_text(s_updates_title, text);
    }
    if (s_update_check_label) {
        const char *text = i18n_manager_get_string("settings_updates_check");
        if (!text) {
            text = "Check";
        }
        lv_label_set_text(s_update_check_label, text);
    }
    if (s_update_apply_label) {
        const char *text = i18n_manager_get_string("settings_updates_apply");
        if (!text) {
            text = "Apply";
        }
        lv_label_set_text(s_update_apply_label, text);
    }

    s_events_suspended = false;

#if CONFIG_APP_ENABLE_TTS_STUB
    ui_settings_update_tts_state();
#endif
    ui_settings_update_profiles_status();
    ui_settings_update_update_status_label();
    ui_settings_updates_refresh_last_flash();
}

static void ui_settings_update_language_options(void)
{
    if (!s_language_dropdown) {
        return;
    }

    size_t offset = 0;
    size_t remaining = sizeof(s_language_options_buffer);
    for (size_t i = 0; i < (sizeof(s_language_options) / sizeof(s_language_options[0])); ++i) {
        const char *label = i18n_manager_get_string(s_language_options[i].label_key);
        if (!label) {
            label = s_language_options[i].code;
        }
        size_t needed = strlen(label) + 1;
        if (needed + 1 > remaining) {
            break;
        }
        strcpy(&s_language_options_buffer[offset], label);
        offset += strlen(label);
        remaining = sizeof(s_language_options_buffer) - offset;
        if (i + 1 < (sizeof(s_language_options) / sizeof(s_language_options[0]))) {
            s_language_options_buffer[offset++] = '\n';
            remaining = sizeof(s_language_options_buffer) - offset;
        }
    }
    s_language_options_buffer[offset] = '\0';

    uint16_t selected = lv_dropdown_get_selected(s_language_dropdown);
    s_events_suspended = true;
    lv_dropdown_set_options(s_language_dropdown, s_language_options_buffer);
    if (selected >= (sizeof(s_language_options) / sizeof(s_language_options[0]))) {
        selected = 0;
    }
    lv_dropdown_set_selected(s_language_dropdown, selected);
    s_events_suspended = false;
}

static void ui_settings_update_profiles_status(void)
{
    if (!s_profiles_status_label) {
        return;
    }

    if (s_profiles_pending) {
        const char *text = i18n_manager_get_string("settings_profiles_reload_request");
        if (!text) {
            text = "Reload requested...";
        }
        lv_label_set_text(s_profiles_status_label, text);
        return;
    }

    if (!s_profiles_status_initialized) {
        const char *text = i18n_manager_get_string("settings_profiles_status_idle");
        if (!text) {
            text = "Awaiting reload request";
        }
        lv_label_set_text(s_profiles_status_label, text);
        return;
    }

    if (s_profiles_last_status == ESP_OK) {
        const char *fmt = i18n_manager_get_string("settings_profiles_status_success_fmt");
        if (!fmt) {
            fmt = "Profiles reloaded (%u terrariums)";
        }
        lv_label_set_text_fmt(s_profiles_status_label, fmt, (unsigned)s_profiles_last_count);
        return;
    }

    if (s_profiles_last_status == ESP_ERR_NOT_FOUND) {
        const char *fmt = i18n_manager_get_string("settings_profiles_status_fallback_fmt");
        if (!fmt) {
            fmt = "Fallback to built-in profiles (%u terrariums)";
        }
        lv_label_set_text_fmt(s_profiles_status_label, fmt, (unsigned)s_profiles_last_count);
        return;
    }

    const char *fmt = i18n_manager_get_string("settings_profiles_status_error_fmt");
    if (!fmt) {
        fmt = "Profile reload failed: %s";
    }
    lv_label_set_text_fmt(s_profiles_status_label, fmt, esp_err_to_name(s_profiles_last_status));
}

static void ui_settings_update_update_status_label(void)
{
    if (!s_update_status_label) {
        return;
    }

    switch (s_update_state) {
        case UI_SETTINGS_UPDATE_STATUS_AVAILABLE: {
            const char *fmt = i18n_manager_get_string("settings_updates_status_available_fmt");
            if (!fmt) {
                fmt = "Available: v%s (%u KiB) CRC %08X";
            }
            const char *version = s_update_info.version[0] ? s_update_info.version : "?";
            uint32_t size_kib = (uint32_t)((s_update_info.size_bytes + 1023U) / 1024U);
            lv_label_set_text_fmt(s_update_status_label,
                                  fmt,
                                  version,
                                  (unsigned)size_kib,
                                  (unsigned)s_update_info.crc32);
            if (s_update_apply_btn) {
                lv_obj_clear_state(s_update_apply_btn, LV_STATE_DISABLED);
            }
            break;
        }
        case UI_SETTINGS_UPDATE_STATUS_NONE: {
            const char *text = i18n_manager_get_string("settings_updates_status_none");
            if (!text) {
                text = "No update detected";
            }
            lv_label_set_text(s_update_status_label, text);
            if (s_update_apply_btn) {
                lv_obj_add_state(s_update_apply_btn, LV_STATE_DISABLED);
            }
            break;
        }
        case UI_SETTINGS_UPDATE_STATUS_ERROR: {
            const char *fmt = i18n_manager_get_string("settings_updates_status_error_fmt");
            if (!fmt) {
                fmt = "Error: %s";
            }
            lv_label_set_text_fmt(s_update_status_label, fmt, esp_err_to_name(s_update_last_error));
            if (s_update_apply_btn) {
                lv_obj_add_state(s_update_apply_btn, LV_STATE_DISABLED);
            }
            break;
        }
        case UI_SETTINGS_UPDATE_STATUS_APPLIED: {
            const char *text = i18n_manager_get_string("settings_updates_apply_success");
            if (!text) {
                text = "Update copied. Reboot required.";
            }
            lv_label_set_text(s_update_status_label, text);
            if (s_update_apply_btn) {
                lv_obj_add_state(s_update_apply_btn, LV_STATE_DISABLED);
            }
            break;
        }
        case UI_SETTINGS_UPDATE_STATUS_IDLE:
        default: {
            const char *text = i18n_manager_get_string("settings_updates_status_idle");
            if (!text) {
                text = "Awaiting check";
            }
            lv_label_set_text(s_update_status_label, text);
            if (s_update_apply_btn) {
                lv_obj_add_state(s_update_apply_btn, LV_STATE_DISABLED);
            }
            break;
        }
    }
}

#if CONFIG_APP_ENABLE_TTS_STUB
static void ui_settings_tts_changed_cb(lv_event_t *event)
{
    if (s_events_suspended || !event) {
        return;
    }
    bool enabled = lv_obj_has_state(s_tts_switch, LV_STATE_CHECKED);
    tts_stub_enable(enabled);
    ESP_LOGI(TAG, "TTS stub %s", enabled ? "enabled" : "disabled");
}

static void ui_settings_update_tts_state(void)
{
    if (!s_tts_switch) {
        return;
    }
    bool enabled = tts_stub_is_enabled();
    s_events_suspended = true;
    if (enabled) {
        lv_obj_add_state(s_tts_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_tts_switch, LV_STATE_CHECKED);
    }
    s_events_suspended = false;
}
#endif
