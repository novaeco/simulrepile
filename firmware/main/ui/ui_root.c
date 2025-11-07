#include "ui/ui_root.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include <string.h>

#include "lvgl.h"
#include "lvgl_port.h"
#include "sim/sim_engine.h"
#include "ui/ui_dashboard.h"
#include "ui/ui_docs.h"
#include "ui/ui_settings.h"
#include "ui/ui_slots.h"
#include "ui/ui_theme.h"

static const char *TAG = "ui_root";
static const char *UI_ROOT_ALERT_DEFAULT_TEXT = "Connexion DevKitC perdue. Simulation locale active.";
#define UI_ROOT_ALERT_TEXT_MAX 192

static lv_obj_t *s_screen_boot = NULL;
static lv_obj_t *s_screen_disclaimer = NULL;
static lv_obj_t *s_screen_main = NULL;
static lv_obj_t *s_tabview = NULL;
static lv_obj_t *s_tab_dashboard = NULL;
static lv_obj_t *s_tab_slots = NULL;
static lv_obj_t *s_tab_docs = NULL;
static lv_obj_t *s_tab_settings = NULL;
static ui_root_view_t s_active_view = UI_ROOT_VIEW_BOOT_SPLASH;
static lv_obj_t *s_alert_banner = NULL;
static lv_obj_t *s_alert_label = NULL;
static bool s_alert_visible = false;
static char s_alert_message[UI_ROOT_ALERT_TEXT_MAX] = "";

static void ui_root_build_boot_screen(void);
static void ui_root_build_disclaimer_screen(void);
static void ui_root_build_main_screen(void);
static void ui_root_on_disclaimer_accepted(lv_event_t *event);
static void ui_root_on_tab_changed(lv_event_t *event);

esp_err_t ui_root_init(void)
{
    ESP_LOGI(TAG, "Initializing UI root");

    lvgl_port_lock();

    ui_theme_apply_default();
    if (CONFIG_APP_THEME_HIGH_CONTRAST) {
        ui_theme_apply_high_contrast(true);
    }

    ui_root_build_boot_screen();
    ui_root_build_disclaimer_screen();
    ui_root_build_main_screen();

    s_active_view = UI_ROOT_VIEW_BOOT_SPLASH;
    if (s_screen_boot) {
        lv_screen_load(s_screen_boot);
    }

    lvgl_port_unlock();

    return ESP_OK;
}

void ui_root_show_boot_splash(void)
{
    ESP_LOGI(TAG, "Displaying splash screen");
    ui_root_set_view(UI_ROOT_VIEW_BOOT_SPLASH);
}

void ui_root_show_disclaimer(void)
{
    ESP_LOGI(TAG, "Displaying disclaimer overlay");
    ui_root_set_view(UI_ROOT_VIEW_DISCLAIMER);
}

void ui_root_show_dashboard(void)
{
    ui_root_set_view(UI_ROOT_VIEW_DASHBOARD);
}

void ui_root_show_slots(void)
{
    ui_root_set_view(UI_ROOT_VIEW_SLOTS);
}

void ui_root_show_docs(void)
{
    ui_root_set_view(UI_ROOT_VIEW_DOCS);
}

void ui_root_show_settings(void)
{
    ui_root_set_view(UI_ROOT_VIEW_SETTINGS);
}

void ui_root_update(void)
{
    lvgl_port_lock();
    size_t terrarium_count = sim_engine_get_count();
    const terrarium_state_t *first_state = terrarium_count > 0 ? sim_engine_get_state(0) : NULL;
    ui_dashboard_refresh(terrarium_count, first_state);
    ui_slots_refresh();
    lvgl_port_unlock();
}

esp_err_t ui_root_set_view(ui_root_view_t view)
{
    lv_obj_t *target = NULL;
    lvgl_port_lock();

    switch (view) {
    case UI_ROOT_VIEW_BOOT_SPLASH:
        target = s_screen_boot;
        break;
    case UI_ROOT_VIEW_DISCLAIMER:
        target = s_screen_disclaimer;
        break;
    case UI_ROOT_VIEW_DASHBOARD:
        target = s_screen_main;
        if (s_tabview) {
            lv_tabview_set_active(s_tabview, 0, LV_ANIM_OFF);
        }
        break;
    case UI_ROOT_VIEW_SLOTS:
        target = s_screen_main;
        if (s_tabview) {
            lv_tabview_set_active(s_tabview, 1, LV_ANIM_OFF);
        }
        break;
    case UI_ROOT_VIEW_DOCS:
        target = s_screen_main;
        if (s_tabview) {
            lv_tabview_set_active(s_tabview, 2, LV_ANIM_OFF);
        }
        break;
    case UI_ROOT_VIEW_SETTINGS:
        target = s_screen_main;
        if (s_tabview) {
            lv_tabview_set_active(s_tabview, 3, LV_ANIM_OFF);
        }
        break;
    default:
        break;
    }

    if (!target) {
        lvgl_port_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    lv_screen_load(target);
    s_active_view = view;
    lvgl_port_unlock();
    return ESP_OK;
}

void ui_root_set_link_alert(bool visible, const char *message)
{
    const char *resolved = message;
    if (visible) {
        if (!resolved || resolved[0] == '\0') {
            resolved = UI_ROOT_ALERT_DEFAULT_TEXT;
        }
    } else if (!resolved) {
        resolved = "";
    }

    lvgl_port_lock();
    s_alert_visible = visible;
    if (resolved) {
        strncpy(s_alert_message, resolved, UI_ROOT_ALERT_TEXT_MAX - 1);
        s_alert_message[UI_ROOT_ALERT_TEXT_MAX - 1] = '\0';
    } else {
        s_alert_message[0] = '\0';
    }

    if (s_alert_label) {
        lv_label_set_text(s_alert_label, s_alert_message);
    }
    if (s_alert_banner) {
        if (visible) {
            lv_obj_clear_flag(s_alert_banner, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_alert_banner, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lvgl_port_unlock();
}

static void ui_root_build_boot_screen(void)
{
    s_screen_boot = lv_obj_create(NULL);
    ui_theme_apply_screen_style(s_screen_boot);
    lv_obj_set_style_pad_all(s_screen_boot, 32, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_screen_boot, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen_boot, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(s_screen_boot);
    lv_label_set_text(title, "SimulRepile");
    ui_theme_apply_label_style(title, true);

    lv_obj_t *subtitle = lv_label_create(s_screen_boot);
    lv_label_set_text(subtitle, "Simulateur pédagogique terrariophile");
    ui_theme_apply_label_style(subtitle, false);

    lv_obj_t *progress = lv_label_create(s_screen_boot);
    lv_label_set_text(progress, "Initialisation des modules...");
    ui_theme_apply_label_style(progress, false);
}

static void ui_root_build_disclaimer_screen(void)
{
    s_screen_disclaimer = lv_obj_create(NULL);
    ui_theme_apply_screen_style(s_screen_disclaimer);
    lv_obj_set_style_pad_all(s_screen_disclaimer, 24, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_screen_disclaimer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen_disclaimer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *panel = lv_obj_create(s_screen_disclaimer);
    ui_theme_apply_panel_style(panel);
    lv_obj_set_size(panel, LV_PCT(80), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(panel, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Mentions légales");
    ui_theme_apply_label_style(title, true);
    lv_obj_set_width(title, LV_PCT(100));

    lv_obj_t *content = lv_label_create(panel);
    lv_label_set_text(content,
                      "Ce simulateur rappelle les obligations légales liées à la détention d'espèces protégées.\n"
                      "Aucune action réalisée ici ne dispense des autorisations officielles (CITES, I-FAP, CDC)."
                      " Respectez le bien-être animal et les réglementations en vigueur.");
    lv_label_set_long_mode(content, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(content, LV_PCT(100));
    ui_theme_apply_label_style(content, false);

    lv_obj_t *button = lv_button_create(panel);
    lv_obj_set_width(button, LV_PCT(40));
    lv_obj_add_event_cb(button, ui_root_on_disclaimer_accepted, LV_EVENT_CLICKED, NULL);
    ui_theme_apply_panel_style(button);

    lv_obj_t *button_label = lv_label_create(button);
    lv_label_set_text(button_label, "J'ai compris");
    ui_theme_apply_label_style(button_label, true);
    lv_obj_center(button_label);
}

static void ui_root_build_main_screen(void)
{
    s_screen_main = lv_obj_create(NULL);
    ui_theme_apply_screen_style(s_screen_main);
    lv_obj_set_style_pad_all(s_screen_main, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_screen_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen_main, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_alert_banner = lv_obj_create(s_screen_main);
    lv_obj_set_width(s_alert_banner, LV_PCT(100));
    lv_obj_set_style_bg_color(s_alert_banner, lv_color_hex(0xB71C1C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_alert_banner, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_alert_banner, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_alert_banner, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_alert_banner, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_alert_banner, 0, LV_PART_MAIN);

    s_alert_label = lv_label_create(s_alert_banner);
    ui_theme_apply_label_style(s_alert_label, true);
    lv_label_set_long_mode(s_alert_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_alert_label, LV_PCT(100));
    lv_obj_set_style_text_color(s_alert_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    if (s_alert_message[0] != '\0') {
        lv_label_set_text(s_alert_label, s_alert_message);
    } else {
        lv_label_set_text(s_alert_label, UI_ROOT_ALERT_DEFAULT_TEXT);
    }
    if (!s_alert_visible) {
        lv_obj_add_flag(s_alert_banner, LV_OBJ_FLAG_HIDDEN);
    }

    s_tabview = lv_tabview_create(s_screen_main, LV_DIR_TOP, 64);
    lv_obj_set_flex_grow(s_tabview, 1);
    lv_obj_set_size(s_tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_tabview, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_event_cb(s_tabview, ui_root_on_tab_changed, LV_EVENT_VALUE_CHANGED, NULL);

    s_tab_dashboard = lv_tabview_add_tab(s_tabview, "Tableau de bord");
    s_tab_slots = lv_tabview_add_tab(s_tabview, "Terrariums");
    s_tab_docs = lv_tabview_add_tab(s_tabview, "Documents");
    s_tab_settings = lv_tabview_add_tab(s_tabview, "Paramètres");

    ui_dashboard_create(s_tab_dashboard);
    ui_slots_create(s_tab_slots);
    ui_docs_create(s_tab_docs);
    ui_settings_create(s_tab_settings);
}

static void ui_root_on_disclaimer_accepted(lv_event_t *event)
{
    (void)event;
    ESP_LOGI(TAG, "Disclaimer acknowledged");
    ui_root_show_dashboard();
}

static void ui_root_on_tab_changed(lv_event_t *event)
{
    if (!event || !s_tabview) {
        return;
    }

    uint16_t index = lv_tabview_get_tab_act(s_tabview);
    switch (index) {
    case 0:
        s_active_view = UI_ROOT_VIEW_DASHBOARD;
        break;
    case 1:
        s_active_view = UI_ROOT_VIEW_SLOTS;
        ui_slots_refresh();
        break;
    case 2:
        s_active_view = UI_ROOT_VIEW_DOCS;
        ui_docs_refresh_category();
        break;
    case 3:
        s_active_view = UI_ROOT_VIEW_SETTINGS;
        break;
    default:
        break;
    }
}
