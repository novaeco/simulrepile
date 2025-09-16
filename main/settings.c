#include "settings.h"
#include "env_control.h"
#include "lvgl.h"
#include "nvs.h"
#include "sleep.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NVS_NS "cfg"
#define KEY_ENV "env_cfg"
#define KEY_SLEEP "sleep_def"
#define KEY_LOG "log_lvl"

#define DEFAULT_SLEEP true
#define DEFAULT_LOG_LEVEL ESP_LOG_INFO
#define SPIN_SCALE_1DP 10

app_settings_t g_settings = {0};

static lv_obj_t *screen;
static lv_obj_t *sw_sleep;
static lv_obj_t *dd_log;
static lv_obj_t *sb_count;
static lv_obj_t *sb_period;

typedef struct {
    lv_obj_t *tab;
    lv_obj_t *name;
    lv_obj_t *enabled;
    lv_obj_t *day_temp;
    lv_obj_t *day_hum;
    lv_obj_t *night_temp;
    lv_obj_t *night_hum;
    lv_obj_t *heat_on;
    lv_obj_t *heat_off;
    lv_obj_t *hum_on;
    lv_obj_t *hum_off;
    lv_obj_t *day_start_hour;
    lv_obj_t *day_start_min;
    lv_obj_t *night_start_hour;
    lv_obj_t *night_start_min;
    lv_obj_t *uv_enabled;
    lv_obj_t *uv_on_hour;
    lv_obj_t *uv_on_min;
    lv_obj_t *uv_off_hour;
    lv_obj_t *uv_off_min;
    lv_obj_t *min_heat;
    lv_obj_t *min_pump;
} terrarium_widgets_t;

static terrarium_widgets_t s_t_widgets[REPTILE_ENV_MAX_TERRARIUMS];
static lv_obj_t *terrarium_tabs[REPTILE_ENV_MAX_TERRARIUMS];

extern lv_obj_t *menu_screen;

static lv_obj_t *create_label(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, txt);
    return label;
}

static lv_obj_t *create_spinbox_int(lv_obj_t *parent, int32_t min, int32_t max, int32_t step, int32_t value)
{
    lv_obj_t *sb = lv_spinbox_create(parent);
    lv_spinbox_set_range(sb, min, max);
    lv_spinbox_set_step(sb, step);
    lv_spinbox_set_digit_format(sb, 3, 0);
    lv_spinbox_set_value(sb, value);
    return sb;
}

static lv_obj_t *create_spinbox_1dp(lv_obj_t *parent, float min, float max, float step, float value)
{
    lv_obj_t *sb = lv_spinbox_create(parent);
    int32_t imin = (int32_t)lroundf(min * SPIN_SCALE_1DP);
    int32_t imax = (int32_t)lroundf(max * SPIN_SCALE_1DP);
    int32_t istep = (int32_t)lroundf(step * SPIN_SCALE_1DP);
    int32_t ivalue = (int32_t)lroundf(value * SPIN_SCALE_1DP);
    lv_spinbox_set_range(sb, imin, imax);
    lv_spinbox_set_step(sb, istep);
    lv_spinbox_set_digit_format(sb, 4, 1);
    lv_spinbox_set_value(sb, ivalue);
    return sb;
}

static void apply_count_visibility(uint32_t count)
{
    for (size_t i = 0; i < REPTILE_ENV_MAX_TERRARIUMS; ++i) {
        if (!terrarium_tabs[i]) {
            continue;
        }
        if (i < count) {
            lv_obj_clear_flag(terrarium_tabs[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(terrarium_tabs[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void count_changed_cb(lv_event_t *e)
{
    (void)e;
    uint32_t count = (uint32_t)lv_spinbox_get_value(sb_count);
    apply_count_visibility(count);
}

void settings_apply(void)
{
    sleep_set_enabled(g_settings.sleep_default);
    esp_log_level_set("*", g_settings.log_level);
    reptile_env_update_config(&g_settings.env_config);
}

esp_err_t settings_save(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nvs, KEY_ENV, &g_settings.env_config, sizeof(g_settings.env_config));
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, KEY_SLEEP, g_settings.sleep_default);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, KEY_LOG, g_settings.log_level);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t settings_init(void)
{
    reptile_env_get_default_config(&g_settings.env_config);
    g_settings.sleep_default = DEFAULT_SLEEP;
    g_settings.log_level = DEFAULT_LOG_LEVEL;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        size_t size = sizeof(g_settings.env_config);
        if (nvs_get_blob(nvs, KEY_ENV, &g_settings.env_config, &size) != ESP_OK) {
            // keep defaults
        }
        uint8_t val8;
        if (nvs_get_u8(nvs, KEY_SLEEP, &val8) == ESP_OK) {
            g_settings.sleep_default = val8;
        }
        if (nvs_get_u8(nvs, KEY_LOG, &val8) == ESP_OK) {
            g_settings.log_level = val8;
        }
        nvs_close(nvs);
    }

    if (g_settings.env_config.terrarium_count == 0 ||
        g_settings.env_config.terrarium_count > REPTILE_ENV_MAX_TERRARIUMS) {
        g_settings.env_config.terrarium_count = 1;
    }

    settings_apply();
    return ESP_OK;
}

static void populate_terrarium_tab(size_t index,
                                   lv_obj_t *tab,
                                   const reptile_env_terrarium_config_t *cfg)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(tab, 12, 0);

    terrarium_widgets_t *w = &s_t_widgets[index];
    w->tab = tab;

    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    create_label(row, "Activé");
    w->enabled = lv_switch_create(row);
    if (cfg->enabled) {
        lv_obj_add_state(w->enabled, LV_STATE_CHECKED);
    }
    create_label(row, "Nom");
    w->name = lv_textarea_create(row);
    lv_textarea_set_one_line(w->name, true);
    lv_textarea_set_max_length(w->name, sizeof(cfg->name) - 1);
    lv_textarea_set_text(w->name, cfg->name);
    lv_obj_set_flex_grow(w->name, 1);

    row = lv_obj_create(tab);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 12, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    create_label(row, "Jour Temp °C");
    w->day_temp = create_spinbox_1dp(row, 10.0f, 45.0f, 0.5f, cfg->day.temperature_c);
    create_label(row, "Jour Hum %");
    w->day_hum = create_spinbox_int(row, 0, 100, 1, (int32_t)lroundf(cfg->day.humidity_pct));

    row = lv_obj_create(tab);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 12, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    create_label(row, "Nuit Temp °C");
    w->night_temp = create_spinbox_1dp(row, 5.0f, 40.0f, 0.5f, cfg->night.temperature_c);
    create_label(row, "Nuit Hum %");
    w->night_hum = create_spinbox_int(row, 0, 100, 1, (int32_t)lroundf(cfg->night.humidity_pct));

    row = lv_obj_create(tab);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    create_label(row, "Hyst. Chauffage ON °C");
    w->heat_on = create_spinbox_1dp(row, 0.5f, 10.0f, 0.1f, cfg->hysteresis.heat_on_delta);
    create_label(row, "OFF °C");
    w->heat_off = create_spinbox_1dp(row, 0.1f, 10.0f, 0.1f, cfg->hysteresis.heat_off_delta);
    create_label(row, "Hyst. Humid. ON %");
    w->hum_on = create_spinbox_1dp(row, 1.0f, 30.0f, 0.5f, cfg->hysteresis.humidity_on_delta);
    create_label(row, "OFF %");
    w->hum_off = create_spinbox_1dp(row, 1.0f, 30.0f, 0.5f, cfg->hysteresis.humidity_off_delta);

    row = lv_obj_create(tab);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    create_label(row, "Début jour");
    w->day_start_hour = create_spinbox_int(row, 0, 23, 1, cfg->day_start.hour);
    create_label(row, ":");
    w->day_start_min = create_spinbox_int(row, 0, 59, 5, cfg->day_start.minute);
    create_label(row, "Début nuit");
    w->night_start_hour = create_spinbox_int(row, 0, 23, 1, cfg->night_start.hour);
    create_label(row, ":");
    w->night_start_min = create_spinbox_int(row, 0, 59, 5, cfg->night_start.minute);

    row = lv_obj_create(tab);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    create_label(row, "UV auto");
    w->uv_enabled = lv_switch_create(row);
    if (cfg->uv.enabled) {
        lv_obj_add_state(w->uv_enabled, LV_STATE_CHECKED);
    }
    create_label(row, "ON");
    w->uv_on_hour = create_spinbox_int(row, 0, 23, 1, cfg->uv.on.hour);
    create_label(row, ":");
    w->uv_on_min = create_spinbox_int(row, 0, 59, 5, cfg->uv.on.minute);
    create_label(row, "OFF");
    w->uv_off_hour = create_spinbox_int(row, 0, 23, 1, cfg->uv.off.hour);
    create_label(row, ":");
    w->uv_off_min = create_spinbox_int(row, 0, 59, 5, cfg->uv.off.minute);

    row = lv_obj_create(tab);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 12, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    create_label(row, "Intervalle min chauffage (min)");
    w->min_heat = create_spinbox_int(row, 0, 240, 1, cfg->min_minutes_between_heat);
    create_label(row, "Intervalle min pompe (min)");
    w->min_pump = create_spinbox_int(row, 0, 240, 1, cfg->min_minutes_between_pump);
}

static void save_btn_cb(lv_event_t *e)
{
    (void)e;
    g_settings.sleep_default = lv_obj_has_state(sw_sleep, LV_STATE_CHECKED);
    g_settings.log_level = lv_dropdown_get_selected(dd_log);
    g_settings.env_config.terrarium_count = (size_t)lv_spinbox_get_value(sb_count);
    if (g_settings.env_config.terrarium_count == 0) {
        g_settings.env_config.terrarium_count = 1;
    }
    g_settings.env_config.period_ms = (uint32_t)lv_spinbox_get_value(sb_period);

    for (size_t i = 0; i < REPTILE_ENV_MAX_TERRARIUMS; ++i) {
        terrarium_widgets_t *w = &s_t_widgets[i];
        reptile_env_terrarium_config_t *cfg = &g_settings.env_config.terrarium[i];
        const char *name = lv_textarea_get_text(w->name);
        if (!name) {
            name = "";
        }
        strncpy(cfg->name, name, sizeof(cfg->name) - 1);
        cfg->name[sizeof(cfg->name) - 1] = '\0';
        cfg->enabled = lv_obj_has_state(w->enabled, LV_STATE_CHECKED);
        cfg->day.temperature_c = (float)lv_spinbox_get_value(w->day_temp) / SPIN_SCALE_1DP;
        cfg->day.humidity_pct = (float)lv_spinbox_get_value(w->day_hum);
        cfg->night.temperature_c = (float)lv_spinbox_get_value(w->night_temp) / SPIN_SCALE_1DP;
        cfg->night.humidity_pct = (float)lv_spinbox_get_value(w->night_hum);
        cfg->hysteresis.heat_on_delta = (float)lv_spinbox_get_value(w->heat_on) / SPIN_SCALE_1DP;
        cfg->hysteresis.heat_off_delta = (float)lv_spinbox_get_value(w->heat_off) / SPIN_SCALE_1DP;
        cfg->hysteresis.humidity_on_delta = (float)lv_spinbox_get_value(w->hum_on) / SPIN_SCALE_1DP;
        cfg->hysteresis.humidity_off_delta = (float)lv_spinbox_get_value(w->hum_off) / SPIN_SCALE_1DP;
        cfg->day_start.hour = (uint8_t)lv_spinbox_get_value(w->day_start_hour);
        cfg->day_start.minute = (uint8_t)lv_spinbox_get_value(w->day_start_min);
        cfg->night_start.hour = (uint8_t)lv_spinbox_get_value(w->night_start_hour);
        cfg->night_start.minute = (uint8_t)lv_spinbox_get_value(w->night_start_min);
        cfg->uv.enabled = lv_obj_has_state(w->uv_enabled, LV_STATE_CHECKED);
        cfg->uv.on.hour = (uint8_t)lv_spinbox_get_value(w->uv_on_hour);
        cfg->uv.on.minute = (uint8_t)lv_spinbox_get_value(w->uv_on_min);
        cfg->uv.off.hour = (uint8_t)lv_spinbox_get_value(w->uv_off_hour);
        cfg->uv.off.minute = (uint8_t)lv_spinbox_get_value(w->uv_off_min);
        cfg->min_minutes_between_heat = (uint32_t)lv_spinbox_get_value(w->min_heat);
        cfg->min_minutes_between_pump = (uint32_t)lv_spinbox_get_value(w->min_pump);
    }

    settings_save();
    settings_apply();
    lv_scr_load(menu_screen);
    lv_obj_del_async(screen);
}

void settings_screen_show(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(screen, 10, 0);
    lv_obj_set_style_pad_gap(screen, 12, 0);

    lv_obj_t *tv = lv_tabview_create(screen);
    lv_tabview_set_tab_bar_position(tv, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tv, 40);
    lv_obj_set_size(tv, LV_PCT(100), LV_PCT(100));

    lv_obj_t *tab_general = lv_tabview_add_tab(tv, "Général");
    lv_obj_set_flex_flow(tab_general, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab_general, 12, 0);
    lv_obj_set_style_pad_gap(tab_general, 12, 0);

    create_label(tab_general, "Nombre de terrariums");
    sb_count = create_spinbox_int(tab_general, 1, REPTILE_ENV_MAX_TERRARIUMS, 1,
                                  (int32_t)g_settings.env_config.terrarium_count);
    lv_obj_add_event_cb(sb_count, count_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    create_label(tab_general, "Période boucle (ms)");
    sb_period = create_spinbox_int(tab_general, 200, 10000, 100,
                                   (int32_t)g_settings.env_config.period_ms);

    create_label(tab_general, "Veille par défaut");
    sw_sleep = lv_switch_create(tab_general);
    if (g_settings.sleep_default) {
        lv_obj_add_state(sw_sleep, LV_STATE_CHECKED);
    }

    create_label(tab_general, "Niveau log");
    dd_log = lv_dropdown_create(tab_general);
    lv_dropdown_set_options_static(dd_log,
                                   "NONE\nERROR\nWARN\nINFO\nDEBUG\nVERBOSE");
    lv_dropdown_set_selected(dd_log, g_settings.log_level);

    reptile_env_config_t defaults;
    reptile_env_get_default_config(&defaults);

    for (size_t i = 0; i < REPTILE_ENV_MAX_TERRARIUMS; ++i) {
        reptile_env_terrarium_config_t cfg = defaults.terrarium[(i < defaults.terrarium_count) ? i : defaults.terrarium_count - 1];
        if (i < g_settings.env_config.terrarium_count) {
            cfg = g_settings.env_config.terrarium[i];
        }
        lv_obj_t *tab = lv_tabview_add_tab(tv, cfg.name[0] ? cfg.name : "Terrarium");
        terrarium_tabs[i] = tab;
        populate_terrarium_tab(i, tab, &cfg);
    }

    apply_count_visibility(g_settings.env_config.terrarium_count);

    lv_obj_t *btn = lv_btn_create(screen);
    lv_obj_set_size(btn, 200, 50);
    lv_obj_add_event_cb(btn, save_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "Sauver");
    lv_obj_center(label);

    lv_scr_load(screen);
}

