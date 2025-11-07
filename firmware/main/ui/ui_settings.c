#include "ui/ui_settings.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/exio.h"
#include "i18n/i18n_manager.h"
#include "updates/updates_manager.h"
#include "ui/ui_theme.h"
#include "esp_err.h"

typedef struct {
    const char *label;
    const char *code;
    i18n_language_t language;
} ui_settings_language_option_t;

static const ui_settings_language_option_t s_language_options[] = {
    {.label = "Français (FR)", .code = "fr", .language = I18N_LANG_FR},
    {.label = "English (EN)", .code = "en", .language = I18N_LANG_EN},
    {.label = "Deutsch (DE)", .code = "de", .language = I18N_LANG_DE},
    {.label = "Español (ES)", .code = "es", .language = I18N_LANG_ES},
};

static const char *TAG = "ui_settings";

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_language_dropdown = NULL;
static lv_obj_t *s_contrast_switch = NULL;
static lv_obj_t *s_autosave_slider = NULL;
static lv_obj_t *s_autosave_label = NULL;
static lv_obj_t *s_usb_switch = NULL;
static lv_obj_t *s_update_status_label = NULL;
static lv_obj_t *s_update_check_btn = NULL;
static lv_obj_t *s_update_apply_btn = NULL;

static uint32_t s_autosave_interval_s = CONFIG_APP_AUTOSAVE_INTERVAL_S;
static bool s_usb_selected = true;
static bool s_events_suspended = false;
static bool s_update_available = false;
static updates_manifest_info_t s_update_info;

static void ui_settings_build_layout(lv_obj_t *parent);
static void ui_settings_update_autosave_label(void);
static void ui_settings_language_changed_cb(lv_event_t *event);
static void ui_settings_contrast_changed_cb(lv_event_t *event);
static void ui_settings_autosave_changed_cb(lv_event_t *event);
static void ui_settings_usb_changed_cb(lv_event_t *event);
static void ui_settings_updates_refresh(void);
static void ui_settings_updates_check_cb(lv_event_t *event);
static void ui_settings_updates_apply_cb(lv_event_t *event);

void ui_settings_create(lv_obj_t *parent)
{
    if (!parent) {
        return;
    }

    ESP_LOGI(TAG, "Creating settings view");
    ui_settings_build_layout(parent);
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
    } else {
        ESP_LOGI(TAG, "Language switched to %s", s_language_options[index].code);
    }
}

void ui_settings_set_autosave_interval(uint32_t seconds)
{
    if (seconds < 30) {
        seconds = 30;
    }
    if (seconds > 3600) {
        seconds = 3600;
    }

    s_autosave_interval_s = seconds;

    if (s_autosave_slider) {
        s_events_suspended = true;
        lv_slider_set_range(s_autosave_slider, 30, 3600);
        lv_slider_set_value(s_autosave_slider, (int32_t)seconds, LV_ANIM_OFF);
        s_events_suspended = false;
    }
    ui_settings_update_autosave_label();
    ESP_LOGI(TAG, "Autosave interval set to %u s", (unsigned)seconds);
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
        ESP_LOGW(TAG, "Failed to switch USB mode: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Interface basculée en mode %s", usb_enabled ? "USB" : "CAN");
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

    lv_obj_t *language_label = lv_label_create(language_card);
    lv_label_set_text(language_label, "Langue de l'interface");
    ui_theme_apply_label_style(language_label, true);

    s_language_dropdown = lv_dropdown_create(language_card);
    lv_dropdown_set_options_static(s_language_dropdown,
                                   "Français (FR)\nEnglish (EN)\nDeutsch (DE)\nEspañol (ES)");
    lv_obj_add_event_cb(s_language_dropdown, ui_settings_language_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *contrast_card = lv_obj_create(s_root);
    ui_theme_apply_panel_style(contrast_card);
    lv_obj_set_size(contrast_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(contrast_card, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(contrast_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(contrast_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *contrast_label = lv_label_create(contrast_card);
    lv_label_set_text(contrast_label, "Thème contraste élevé");
    ui_theme_apply_label_style(contrast_label, true);

    s_contrast_switch = lv_switch_create(contrast_card);
    lv_obj_add_event_cb(s_contrast_switch, ui_settings_contrast_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *autosave_card = lv_obj_create(s_root);
    ui_theme_apply_panel_style(autosave_card);
    lv_obj_set_size(autosave_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(autosave_card, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(autosave_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(autosave_card, 12, LV_PART_MAIN);

    lv_obj_t *autosave_title = lv_label_create(autosave_card);
    lv_label_set_text(autosave_title, "Sauvegarde automatique");
    ui_theme_apply_label_style(autosave_title, true);

    s_autosave_slider = lv_slider_create(autosave_card);
    lv_slider_set_range(s_autosave_slider, 30, 3600);
    lv_obj_add_event_cb(s_autosave_slider, ui_settings_autosave_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_autosave_label = lv_label_create(autosave_card);
    ui_theme_apply_label_style(s_autosave_label, false);

    lv_obj_t *usb_card = lv_obj_create(s_root);
    ui_theme_apply_panel_style(usb_card);
    lv_obj_set_size(usb_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(usb_card, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(usb_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(usb_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *usb_label = lv_label_create(usb_card);
    lv_label_set_text(usb_label, "Interface USB ↔ CAN");
    ui_theme_apply_label_style(usb_label, true);

    s_usb_switch = lv_switch_create(usb_card);
    lv_obj_add_event_cb(s_usb_switch, ui_settings_usb_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
#if !CONFIG_BSP_USB_CAN_SELECTABLE
    lv_obj_add_state(s_usb_switch, LV_STATE_DISABLED);
#endif

    lv_obj_t *updates_card = lv_obj_create(s_root);
    ui_theme_apply_panel_style(updates_card);
    lv_obj_set_size(updates_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(updates_card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(updates_card, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(updates_card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *updates_title = lv_label_create(updates_card);
    lv_label_set_text(updates_title, "Mise à jour via SD");
    ui_theme_apply_label_style(updates_title, true);

    s_update_status_label = lv_label_create(updates_card);
    ui_theme_apply_label_style(s_update_status_label, false);
    lv_label_set_text(s_update_status_label, "Vérification en attente");

    lv_obj_t *updates_btn_row = lv_obj_create(updates_card);
    lv_obj_set_size(updates_btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(updates_btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(updates_btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(updates_btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(updates_btn_row, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(updates_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(updates_btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_update_check_btn = lv_btn_create(updates_btn_row);
    lv_obj_t *check_label = lv_label_create(s_update_check_btn);
    lv_label_set_text(check_label, "Rechercher");
    lv_obj_center(check_label);
    lv_obj_add_event_cb(s_update_check_btn, ui_settings_updates_check_cb, LV_EVENT_CLICKED, NULL);

    s_update_apply_btn = lv_btn_create(updates_btn_row);
    lv_obj_t *apply_label = lv_label_create(s_update_apply_btn);
    lv_label_set_text(apply_label, "Appliquer");
    lv_obj_center(apply_label);
    lv_obj_add_state(s_update_apply_btn, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_update_apply_btn, ui_settings_updates_apply_cb, LV_EVENT_CLICKED, NULL);
}

static void ui_settings_update_autosave_label(void)
{
    if (!s_autosave_label) {
        return;
    }
    lv_label_set_text_fmt(s_autosave_label, "Intervalle : %u secondes", (unsigned)s_autosave_interval_s);
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

static void ui_settings_updates_refresh(void)
{
    if (!s_update_status_label) {
        return;
    }

    updates_manifest_info_t info;
    esp_err_t err = updates_check_available(&info);
    if (err == ESP_OK) {
        s_update_available = true;
        s_update_info = info;
        uint32_t size_kib = (uint32_t)((info.size_bytes + 1023) / 1024);
        const char *version = info.version[0] ? info.version : "?";
        lv_label_set_text_fmt(s_update_status_label,
                              "Disponible : v%s (%u KiB) CRC %08X",
                              version,
                              (unsigned)size_kib,
                              (unsigned)info.crc32);
        if (s_update_apply_btn) {
            lv_obj_clear_state(s_update_apply_btn, LV_STATE_DISABLED);
        }
    } else if (err == ESP_ERR_NOT_FOUND) {
        s_update_available = false;
        lv_label_set_text(s_update_status_label, "Aucune mise à jour détectée");
        if (s_update_apply_btn) {
            lv_obj_add_state(s_update_apply_btn, LV_STATE_DISABLED);
        }
    } else {
        s_update_available = false;
        lv_label_set_text_fmt(s_update_status_label, "Erreur : %s", esp_err_to_name(err));
        if (s_update_apply_btn) {
            lv_obj_add_state(s_update_apply_btn, LV_STATE_DISABLED);
        }
    }
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
        lv_label_set_text(s_update_status_label, "Copie OTA effectuée. Redémarrage requis.");
        s_update_available = false;
    } else {
        lv_label_set_text_fmt(s_update_status_label, "Échec application : %s", esp_err_to_name(err));
        ui_settings_updates_refresh();
    }

    s_events_suspended = false;
}
