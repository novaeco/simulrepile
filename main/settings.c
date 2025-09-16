#include "settings.h"
#include "lvgl.h"
#include "nvs.h"
#include "sleep.h"
#include <stdio.h>

#define NVS_NS "cfg"
#define KEY_TEMP "temp_th"
#define KEY_HUM  "hum_th"
#define KEY_SLEEP "sleep_def"
#define KEY_LOG   "log_lvl"

#define DEFAULT_TEMP_THRESHOLD 30
#define DEFAULT_HUM_THRESHOLD 50
#define DEFAULT_SLEEP true
#define DEFAULT_LOG_LEVEL ESP_LOG_INFO

app_settings_t g_settings = {
    .temp_threshold = DEFAULT_TEMP_THRESHOLD,
    .humidity_threshold = DEFAULT_HUM_THRESHOLD,
    .sleep_default = DEFAULT_SLEEP,
    .log_level = DEFAULT_LOG_LEVEL,
};

static lv_obj_t *screen;
static lv_obj_t *sb_temp;
static lv_obj_t *sb_hum;
static lv_obj_t *sw_sleep;
static lv_obj_t *dd_log;

extern lv_obj_t *menu_screen;

void settings_apply(void)
{
    sleep_set_enabled(g_settings.sleep_default);
    esp_log_level_set("*", g_settings.log_level);
}

esp_err_t settings_save(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
        return err;
    if ((err = nvs_set_i32(nvs, KEY_TEMP, g_settings.temp_threshold)) != ESP_OK)
        goto out;
    if ((err = nvs_set_i32(nvs, KEY_HUM, g_settings.humidity_threshold)) != ESP_OK)
        goto out;
    if ((err = nvs_set_u8(nvs, KEY_SLEEP, g_settings.sleep_default)) != ESP_OK)
        goto out;
    if ((err = nvs_set_u8(nvs, KEY_LOG, g_settings.log_level)) != ESP_OK)
        goto out;
    err = nvs_commit(nvs);
out:
    nvs_close(nvs);
    return err;
}

esp_err_t settings_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t val32;
        uint8_t val8;
        if (nvs_get_i32(nvs, KEY_TEMP, &val32) == ESP_OK)
            g_settings.temp_threshold = val32;
        if (nvs_get_i32(nvs, KEY_HUM, &val32) == ESP_OK)
            g_settings.humidity_threshold = val32;
        if (nvs_get_u8(nvs, KEY_SLEEP, &val8) == ESP_OK)
            g_settings.sleep_default = val8;
        if (nvs_get_u8(nvs, KEY_LOG, &val8) == ESP_OK)
            g_settings.log_level = val8;
        nvs_close(nvs);
    }
    settings_apply();
    return ESP_OK;
}

static void save_btn_cb(lv_event_t *e)
{
    (void)e;
    g_settings.temp_threshold = lv_spinbox_get_value(sb_temp);
    g_settings.humidity_threshold = lv_spinbox_get_value(sb_hum);
    g_settings.sleep_default = lv_obj_has_state(sw_sleep, LV_STATE_CHECKED);
    g_settings.log_level = lv_dropdown_get_selected(dd_log);
    settings_save();
    settings_apply();
    lv_scr_load(menu_screen);
    lv_obj_del_async(screen);
}

void settings_screen_show(void)
{
    screen = lv_obj_create(NULL);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Seuil Temp °C");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 10);

    sb_temp = lv_spinbox_create(screen);
    lv_spinbox_set_range(sb_temp, 0, 100);
    lv_spinbox_set_value(sb_temp, g_settings.temp_threshold);
    lv_spinbox_set_step(sb_temp, 1);
    lv_obj_align_to(sb_temp, label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    label = lv_label_create(screen);
    lv_label_set_text(label, "Seuil Hum %");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 60);

    sb_hum = lv_spinbox_create(screen);
    lv_spinbox_set_range(sb_hum, 0, 100);
    lv_spinbox_set_value(sb_hum, g_settings.humidity_threshold);
    lv_spinbox_set_step(sb_hum, 1);
    lv_obj_align_to(sb_hum, label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    label = lv_label_create(screen);
    lv_label_set_text(label, "Veille par défaut");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 110);

    sw_sleep = lv_switch_create(screen);
    if (g_settings.sleep_default)
        lv_obj_add_state(sw_sleep, LV_STATE_CHECKED);
    lv_obj_align_to(sw_sleep, label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    label = lv_label_create(screen);
    lv_label_set_text(label, "Niveau log");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 160);

    dd_log = lv_dropdown_create(screen);
    lv_dropdown_set_options_static(dd_log,
                                   "NONE\nERROR\nWARN\nINFO\nDEBUG\nVERBOSE");
    lv_dropdown_set_selected(dd_log, g_settings.log_level);
    lv_obj_align_to(dd_log, label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    lv_obj_t *btn = lv_btn_create(screen);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(btn, save_btn_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn);
    lv_label_set_text(label, "Sauver");
    lv_obj_center(label);

    lv_scr_load(screen);
}

