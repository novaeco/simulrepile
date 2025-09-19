#include "reptile_real.h"
#include "env_control.h"
#include "gpio.h"
#include "sensors.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.h"
#include "logging.h"
#include "ui_theme.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHART_POINT_COUNT 120
#define TEMP_NEEDLE_LENGTH 58

static void feed_task(void *arg);
static void env_state_cb(size_t index, const reptile_env_terrarium_state_t *state, void *ctx);
static void pump_btn_cb(lv_event_t *e);
static void heat_btn_cb(lv_event_t *e);
static void uv_btn_cb(lv_event_t *e);
static void feed_btn_cb(lv_event_t *e);
static void menu_btn_cb(lv_event_t *e);

static lv_obj_t *screen;
static lv_obj_t *feed_status_label;
static volatile bool feed_running;
static TaskHandle_t feed_task_handle;
static size_t s_ui_count;

typedef struct {
    size_t index;
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *status_label;
    lv_obj_t *energy_label;
    lv_obj_t *alarm_label;
    lv_obj_t *temp_scale;
    lv_obj_t *temp_needle;
    lv_obj_t *hum_bar;
    lv_obj_t *btn_heat;
    lv_obj_t *btn_heat_label;
    lv_obj_t *btn_pump;
    lv_obj_t *btn_pump_label;
    lv_obj_t *btn_uv;
    lv_obj_t *btn_uv_label;
    lv_obj_t *uv_state_label;
    lv_obj_t *chart;
    lv_chart_series_t *temp_series;
    lv_chart_series_t *hum_series;
    lv_coord_t temp_points[CHART_POINT_COUNT];
    lv_coord_t hum_points[CHART_POINT_COUNT];
} terrarium_ui_t;

static terrarium_ui_t s_ui[REPTILE_ENV_MAX_TERRARIUMS];
static reptile_env_history_entry_t s_history_buf[REPTILE_ENV_HISTORY_LENGTH];

extern lv_obj_t *menu_screen;

static void update_feed_status(void)
{
    if (!feed_status_label) {
        return;
    }
    lv_label_set_text(feed_status_label, feed_running ? "Nourrissage: ON" : "Nourrissage: OFF");
}

static void feed_task(void *arg)
{
    (void)arg;
    feed_running = true;
    if (lvgl_port_lock(-1)) {
        update_feed_status();
        lvgl_port_unlock();
    }
    reptile_feed_gpio();
    feed_running = false;
    if (lvgl_port_lock(-1)) {
        update_feed_status();
        lvgl_port_unlock();
    }
    feed_task_handle = NULL;
    vTaskDelete(NULL);
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data, lv_obj_t **label_out)
{
    lv_obj_t *btn = ui_theme_create_button(parent, text, UI_THEME_BUTTON_SECONDARY, cb, user_data);
    if (label_out) {
        *label_out = lv_obj_get_child(btn, 0);
    }
    return btn;
}

static void create_temp_scale(terrarium_ui_t *ui, lv_obj_t *parent)
{
    ui->temp_scale = lv_scale_create(parent);
    lv_obj_set_size(ui->temp_scale, 130, 130);
    lv_scale_set_mode(ui->temp_scale, LV_SCALE_MODE_ROUND_OUTER);
    lv_scale_set_range(ui->temp_scale, 0, 45);
    lv_scale_set_angle_range(ui->temp_scale, 270);
    lv_scale_set_rotation(ui->temp_scale, 135);
    lv_scale_set_total_tick_count(ui->temp_scale, 19);
    lv_scale_set_major_tick_every(ui->temp_scale, 2);
    lv_scale_set_label_show(ui->temp_scale, true);
    lv_obj_set_style_line_width(ui->temp_scale, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(ui->temp_scale, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);

    ui->temp_needle = lv_line_create(ui->temp_scale);
    lv_obj_remove_style_all(ui->temp_needle);
    lv_obj_remove_flag(ui->temp_needle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_width(ui->temp_needle, 4, LV_PART_MAIN);
    lv_obj_set_style_line_color(ui->temp_needle, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(ui->temp_needle, true, LV_PART_MAIN);
    lv_scale_set_line_needle_value(ui->temp_scale, ui->temp_needle, TEMP_NEEDLE_LENGTH, 0);
}

static void init_terrarium_ui(size_t index,
                              terrarium_ui_t *ui,
                              lv_obj_t *parent,
                              const reptile_env_terrarium_config_t *cfg)
{
    memset(ui, 0, sizeof(*ui));
    ui->index = index;
    ui->card = ui_theme_create_card(parent);
    lv_obj_set_width(ui->card, LV_PCT(100));
    lv_obj_set_style_pad_all(ui->card, 20, 0);
    lv_obj_set_style_pad_gap(ui->card, 12, 0);
    lv_obj_set_flex_flow(ui->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(ui->card, LV_SCROLLBAR_MODE_OFF);

    ui->title = lv_label_create(ui->card);
    lv_label_set_text(ui->title, cfg->name[0] ? cfg->name : "Terrarium");
    ui_theme_apply_title(ui->title);

    lv_obj_t *row = lv_obj_create(ui->card);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    create_temp_scale(ui, row);

    ui->hum_bar = lv_bar_create(row);
    lv_bar_set_range(ui->hum_bar, 0, 100);
    lv_obj_set_size(ui->hum_bar, 40, 120);
    lv_bar_set_value(ui->hum_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui->hum_bar, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_PART_INDICATOR);

    row = lv_obj_create(ui->card);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    ui->btn_heat = create_button(row, "Chauffage", heat_btn_cb, ui, &ui->btn_heat_label);
    ui->btn_pump = create_button(row, "Brumiser", pump_btn_cb, ui, &ui->btn_pump_label);
    ui->btn_uv = create_button(row, "UV", uv_btn_cb, ui, &ui->btn_uv_label);

    ui->uv_state_label = lv_label_create(ui->card);
    ui_theme_apply_caption(ui->uv_state_label);
    lv_label_set_text(ui->uv_state_label, "UV: auto");

    ui->status_label = lv_label_create(ui->card);
    ui_theme_apply_body(ui->status_label);
    lv_label_set_text(ui->status_label, "");

    ui->energy_label = lv_label_create(ui->card);
    ui_theme_apply_body(ui->energy_label);
    lv_label_set_text(ui->energy_label, "");

    ui->alarm_label = lv_label_create(ui->card);
    ui_theme_apply_body(ui->alarm_label);
    lv_label_set_text(ui->alarm_label, "");

    ui->chart = lv_chart_create(ui->card);
    lv_chart_set_point_count(ui->chart, CHART_POINT_COUNT);
    lv_chart_set_range(ui->chart, LV_CHART_AXIS_PRIMARY_Y, 0, 45);
    lv_chart_set_range(ui->chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100);
    lv_chart_set_div_line_count(ui->chart, 4, 6);
    lv_chart_set_type(ui->chart, LV_CHART_TYPE_LINE);
    lv_obj_set_height(ui->chart, 160);
    ui->temp_series = lv_chart_add_series(ui->chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    ui->hum_series = lv_chart_add_series(ui->chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_set_ext_y_array(ui->chart, ui->temp_series, ui->temp_points);
    lv_chart_set_ext_y_array(ui->chart, ui->hum_series, ui->hum_points);
    for (size_t i = 0; i < CHART_POINT_COUNT; ++i) {
        ui->temp_points[i] = LV_CHART_POINT_NONE;
        ui->hum_points[i] = LV_CHART_POINT_NONE;
    }
}

static void describe_alarms(uint32_t flags, char *buffer, size_t len)
{
    if (flags == REPTILE_ENV_ALARM_NONE) {
        snprintf(buffer, len, "Aucune alarme");
        return;
    }
    buffer[0] = '\0';
    if (flags & REPTILE_ENV_ALARM_SENSOR_FAILURE) {
        strncat(buffer, "Capteur ", len - strlen(buffer) - 1);
    }
    if (flags & REPTILE_ENV_ALARM_TEMP_LOW) {
        strncat(buffer, "Temp basse ", len - strlen(buffer) - 1);
    }
    if (flags & REPTILE_ENV_ALARM_TEMP_HIGH) {
        strncat(buffer, "Temp haute ", len - strlen(buffer) - 1);
    }
    if (flags & REPTILE_ENV_ALARM_HUM_LOW) {
        strncat(buffer, "Hum basse ", len - strlen(buffer) - 1);
    }
    if (flags & REPTILE_ENV_ALARM_HUM_HIGH) {
        strncat(buffer, "Hum haute ", len - strlen(buffer) - 1);
    }
    if (flags & REPTILE_ENV_ALARM_LIGHT_LOW) {
        strncat(buffer, "Lum basse ", len - strlen(buffer) - 1);
    }
}

static void update_chart(terrarium_ui_t *ui, size_t index)
{
    size_t count = reptile_env_get_history(index, s_history_buf, REPTILE_ENV_HISTORY_LENGTH);
    size_t start = 0;
    if (count > CHART_POINT_COUNT) {
        start = count - CHART_POINT_COUNT;
    }
    size_t out_idx = 0;
    for (size_t i = start; i < count; ++i, ++out_idx) {
        float temp = s_history_buf[i].temperature_c;
        float hum = s_history_buf[i].humidity_pct;
        if (!isfinite(temp)) {
            ui->temp_points[out_idx] = LV_CHART_POINT_NONE;
        } else {
            if (temp < 0) {
                temp = 0;
            }
            if (temp > 45) {
                temp = 45;
            }
            ui->temp_points[out_idx] = (lv_coord_t)lroundf(temp);
        }
        if (!isfinite(hum)) {
            ui->hum_points[out_idx] = LV_CHART_POINT_NONE;
        } else {
            if (hum < 0) {
                hum = 0;
            }
            if (hum > 100) {
                hum = 100;
            }
            ui->hum_points[out_idx] = (lv_coord_t)lroundf(hum);
        }
    }
    for (; out_idx < CHART_POINT_COUNT; ++out_idx) {
        ui->temp_points[out_idx] = LV_CHART_POINT_NONE;
        ui->hum_points[out_idx] = LV_CHART_POINT_NONE;
    }
    lv_chart_refresh(ui->chart);
}

static void update_terrarium_ui(terrarium_ui_t *ui, const reptile_env_terrarium_state_t *state)
{
    if (!ui || !state) {
        return;
    }
    if (state->temperature_valid) {
        float temp = state->temperature_c;
        if (temp < 0) {
            temp = 0;
        }
        if (temp > 45) {
            temp = 45;
        }
        if (ui->temp_scale && ui->temp_needle) {
            lv_scale_set_line_needle_value(ui->temp_scale,
                                           ui->temp_needle,
                                           TEMP_NEEDLE_LENGTH,
                                           (int32_t)lroundf(temp));
        }
    }
    if (state->humidity_valid) {
        float hum = state->humidity_pct;
        if (hum < 0) {
            hum = 0;
        }
        if (hum > 100) {
            hum = 100;
        }
        lv_bar_set_value(ui->hum_bar, (int32_t)lroundf(hum), LV_ANIM_OFF);
    } else {
        lv_bar_set_value(ui->hum_bar, 0, LV_ANIM_OFF);
    }

    char temp_str[16];
    char hum_str[16];
    char lux_str[16];
    char target_lux_str[16];
    if (state->temperature_valid && isfinite(state->temperature_c)) {
        snprintf(temp_str, sizeof(temp_str), "%.1f", state->temperature_c);
    } else {
        snprintf(temp_str, sizeof(temp_str), "N/A");
    }
    if (state->humidity_valid && isfinite(state->humidity_pct)) {
        snprintf(hum_str, sizeof(hum_str), "%.1f", state->humidity_pct);
    } else {
        snprintf(hum_str, sizeof(hum_str), "N/A");
    }
    if (state->light_valid && isfinite(state->light_lux)) {
        snprintf(lux_str, sizeof(lux_str), "%.1f", state->light_lux);
    } else {
        snprintf(lux_str, sizeof(lux_str), "N/A");
    }
    if (state->target_light_lux > 0.0f) {
        snprintf(target_lux_str, sizeof(target_lux_str), "%.0f", state->target_light_lux);
    } else {
        snprintf(target_lux_str, sizeof(target_lux_str), "OFF");
    }

    lv_label_set_text_fmt(ui->status_label,
                          "Temp %s/%.1f°C  Hum %s/%.1f%%  Lum %s/%s lx\nChauffage %s  Pompe %s",
                          temp_str,
                          state->target_temperature_c,
                          hum_str,
                          state->target_humidity_pct,
                          lux_str,
                          target_lux_str,
                          state->heating ? "ON" : "OFF",
                          state->pumping ? "ON" : "OFF");

    float total = state->energy_heat_Wh + state->energy_pump_Wh + state->energy_uv_Wh;
    lv_label_set_text_fmt(ui->energy_label,
                          "Énergie: %.2f Wh (Chauffage %.2f / Pompe %.2f / UV %.2f)",
                          total,
                          state->energy_heat_Wh,
                          state->energy_pump_Wh,
                          state->energy_uv_Wh);

    char alarm_text[128];
    describe_alarms(state->alarm_flags, alarm_text, sizeof(alarm_text));
    lv_label_set_text(ui->alarm_label, alarm_text);
    if (state->alarm_flags != REPTILE_ENV_ALARM_NONE) {
        lv_obj_set_style_border_color(ui->card, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        lv_obj_set_style_border_width(ui->card, 2, LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_color(ui->card, lv_color_hex3(0x444), LV_PART_MAIN);
        lv_obj_set_style_border_width(ui->card, 1, LV_PART_MAIN);
    }

    lv_label_set_text_fmt(ui->btn_heat_label, state->manual_heat ? "Chauffage (man)" : "Chauffage");
    lv_label_set_text_fmt(ui->btn_pump_label, state->manual_pump ? "Brumiser (man)" : "Brumiser");
    lv_label_set_text_fmt(ui->uv_state_label,
                          "UV: %s (%s)",
                          state->uv_light ? "ON" : "OFF",
                          state->manual_uv_override ? "manuel" : "auto");
    lv_label_set_text(ui->btn_uv_label,
                      state->manual_uv_override ? "UV (manuel)" : "UV");

    update_chart(ui, ui->index);
}

static void env_state_cb(size_t index, const reptile_env_terrarium_state_t *state, void *ctx)
{
    (void)ctx;
    logging_real_append(index, state);
    if (!lvgl_port_lock(-1)) {
        return;
    }
    if (index < s_ui_count) {
        update_terrarium_ui(&s_ui[index], state);
    }
    lvgl_port_unlock();
}

static void pump_btn_cb(lv_event_t *e)
{
    terrarium_ui_t *ui = lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    reptile_env_manual_pump(ui->index);
}

static void heat_btn_cb(lv_event_t *e)
{
    terrarium_ui_t *ui = lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    reptile_env_manual_heat(ui->index);
}

static void uv_btn_cb(lv_event_t *e)
{
    terrarium_ui_t *ui = lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    reptile_env_manual_uv_toggle(ui->index);
}

static void feed_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!feed_running) {
        xTaskCreate(feed_task, "feed_task", 2048, NULL, 5, &feed_task_handle);
    }
}

static void menu_btn_cb(lv_event_t *e)
{
    (void)e;
    reptile_env_stop();
    logging_real_stop();
    sensors_deinit();
    if (feed_task_handle) {
        vTaskDelete(feed_task_handle);
        feed_task_handle = NULL;
        feed_running = false;
    }
    reptile_actuators_deinit();

    if (lvgl_port_lock(-1)) {
        lv_scr_load(menu_screen);
        if (screen) {
            lv_obj_del(screen);
            screen = NULL;
        }
        lvgl_port_unlock();
    }
}

void reptile_real_start(esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t tp)
{
    (void)panel;
    (void)tp;

    if (!lvgl_port_lock(-1)) {
        return;
    }

    screen = lv_obj_create(NULL);
    ui_theme_apply_screen(screen);
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(screen, 16, 0);
    lv_obj_set_style_pad_gap(screen, 14, 0);

    lv_obj_t *header = ui_theme_create_card(screen);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_style_pad_all(header, 16, 0);
    lv_obj_set_style_pad_gap(header, 12, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(header);
    ui_theme_apply_title(title);
    lv_label_set_text(title, "Mode réel");

    lv_obj_t *spacer = lv_obj_create(header);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);

    create_button(header, "Menu", menu_btn_cb, NULL, NULL);

    feed_status_label = lv_label_create(screen);
    ui_theme_apply_body(feed_status_label);
    update_feed_status();

    lv_obj_t *feed_btn = ui_theme_create_button(screen, "Nourrir",
                                                UI_THEME_BUTTON_PRIMARY,
                                                feed_btn_cb, NULL);
    lv_obj_set_width(feed_btn, 200);

    const reptile_env_config_t *cfg = &g_settings.env_config;
    s_ui_count = cfg->terrarium_count;
    if (s_ui_count > REPTILE_ENV_MAX_TERRARIUMS) {
        s_ui_count = REPTILE_ENV_MAX_TERRARIUMS;
    }

    for (size_t i = 0; i < s_ui_count; ++i) {
        init_terrarium_ui(i, &s_ui[i], screen, &cfg->terrarium[i]);
    }

    lv_disp_load_scr(screen);
    lvgl_port_unlock();

    logging_real_start(s_ui_count, cfg);
    reptile_env_start(cfg, env_state_cb, NULL);
}

