#include "reptile_real.h"
#include "env_control.h"
#include "gpio.h"
#include "sensors.h"
#include "schedule.h"
#include "logging.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.h"
#include "esp_log.h"
#include <math.h>

#define CHART_POINT_COUNT          60
#define SENSOR_POLL_PERIOD_MS      1000
#define LUX_METER_MAX              2000.0f
#define UV_INDEX_METER_MAX         12.0f
#define TEMP_MIN_DISPLAY           -10.0f
#define TEMP_MAX_DISPLAY           60.0f
#define HUMIDITY_MIN_DISPLAY       0.0f
#define HUMIDITY_MAX_DISPLAY       100.0f
#define SENSOR_TASK_STACK_SIZE     4096
#define SENSOR_TASK_PRIORITY       5

static const char *TAG = "reptile_real";

static void feed_task(void *arg);
static void sensor_poll_task(void *arg);
static void env_state_cb(const reptile_env_state_t *state, void *ctx);
static void pump_btn_cb(lv_event_t *e);
static void heat_btn_cb(lv_event_t *e);
static void feed_btn_cb(lv_event_t *e);
static void fan_btn_cb(lv_event_t *e);
static void uv_btn_cb(lv_event_t *e);
static void light_btn_cb(lv_event_t *e);
static void menu_btn_cb(lv_event_t *e);
static void schedule_btn_cb(lv_event_t *e);
static void update_status_labels(void);
static void update_sensor_widgets(float temp, float hum, float lux, sensor_uv_data_t uv);
static lv_obj_t *create_action_row(lv_obj_t *parent,
                                   lv_obj_t **label_out,
                                   const char *initial_text,
                                   const char *btn_text,
                                   lv_event_cb_t cb);
static void schedule_screen_show(void);
static void schedule_screen_populate(const schedule_config_t *cfg);
static void schedule_screen_collect(schedule_config_t *cfg);
static void schedule_save_cb(lv_event_t *e);
static void schedule_cancel_cb(lv_event_t *e);
static bool enforce_schedule_now(void);
static bool logging_real_sample(logging_real_sample_t *sample);

static lv_obj_t *screen;
static lv_obj_t *label_temp;
static lv_obj_t *label_hum;
static lv_obj_t *label_lux;
static lv_obj_t *label_uv;
static lv_obj_t *label_pump;
static lv_obj_t *label_heat;
static lv_obj_t *label_feed;
static lv_obj_t *label_fan;
static lv_obj_t *label_uv_lamp;
static lv_obj_t *label_light;
static lv_obj_t *schedule_screen;
static lv_obj_t *chart;
static lv_chart_series_t *chart_series_temp;
static lv_chart_series_t *chart_series_hum;
static lv_obj_t *sensor_meter;
static lv_meter_scale_t *meter_scale_lux;
static lv_meter_scale_t *meter_scale_uv;
static lv_meter_indicator_t *needle_lux;
static lv_meter_indicator_t *needle_uv;
static volatile bool feed_running;
static TaskHandle_t feed_task_handle;
static TaskHandle_t sensor_task_handle;
static TaskHandle_t sensor_task_waiter;
static volatile bool sensor_task_running;
static volatile bool sensor_ui_ready;
static reptile_env_state_t s_env_state;
static volatile bool fan_on;
static volatile bool uv_lamp_on;
static volatile bool light_on;
static bool heating_allowed = true;
static float s_last_temp = NAN;
static float s_last_hum = NAN;
static float s_last_lux = NAN;
static sensor_uv_data_t s_last_uv = {
    .uva = NAN,
    .uvb = NAN,
    .uv_index = NAN,
};

static lv_obj_t *slot_switch[SCHEDULE_ACTUATOR_COUNT][SCHEDULE_SLOTS_PER_ACTUATOR];
static lv_obj_t *slot_start_hour[SCHEDULE_ACTUATOR_COUNT][SCHEDULE_SLOTS_PER_ACTUATOR];
static lv_obj_t *slot_start_min[SCHEDULE_ACTUATOR_COUNT][SCHEDULE_SLOTS_PER_ACTUATOR];
static lv_obj_t *slot_end_hour[SCHEDULE_ACTUATOR_COUNT][SCHEDULE_SLOTS_PER_ACTUATOR];
static lv_obj_t *slot_end_min[SCHEDULE_ACTUATOR_COUNT][SCHEDULE_SLOTS_PER_ACTUATOR];

extern lv_obj_t *menu_screen;

static lv_obj_t *create_action_row(lv_obj_t *parent,
                                   lv_obj_t **label_out,
                                   const char *initial_text,
                                   const char *btn_text,
                                   lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    *label_out = lv_label_create(row);
    lv_obj_set_flex_grow(*label_out, 1);
    lv_label_set_text(*label_out, initial_text);

    lv_obj_t *btn = lv_btn_create(row);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, btn_text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return row;
}

static lv_obj_t *create_hour_spinbox(lv_obj_t *parent)
{
    lv_obj_t *sb = lv_spinbox_create(parent);
    lv_spinbox_set_range(sb, 0, 23);
    lv_spinbox_set_digit_format(sb, 2, 0);
    lv_spinbox_set_step(sb, 1);
    lv_spinbox_set_rollover(sb, true);
    lv_obj_set_width(sb, 60);
    return sb;
}

static lv_obj_t *create_minute_spinbox(lv_obj_t *parent)
{
    lv_obj_t *sb = lv_spinbox_create(parent);
    lv_spinbox_set_range(sb, 0, 59);
    lv_spinbox_set_digit_format(sb, 2, 0);
    lv_spinbox_set_step(sb, 1);
    lv_spinbox_set_rollover(sb, true);
    lv_obj_set_width(sb, 60);
    return sb;
}

static const char *schedule_actuator_name(schedule_actuator_t act)
{
    switch (act) {
    case SCHEDULE_ACTUATOR_HEATING:
        return "Chauffage";
    case SCHEDULE_ACTUATOR_UV:
        return "UV";
    case SCHEDULE_ACTUATOR_LIGHTING:
        return "Éclairage";
    case SCHEDULE_ACTUATOR_VENTILATION:
        return "Ventilation";
    default:
        return "";
    }
}

static const schedule_slot_t *schedule_config_get_slot(const schedule_config_t *cfg,
                                                       schedule_actuator_t act,
                                                       uint32_t index)
{
    switch (act) {
    case SCHEDULE_ACTUATOR_HEATING:
        return &cfg->heating[index];
    case SCHEDULE_ACTUATOR_UV:
        return &cfg->uv[index];
    case SCHEDULE_ACTUATOR_LIGHTING:
        return &cfg->lighting[index];
    case SCHEDULE_ACTUATOR_VENTILATION:
        return &cfg->ventilation[index];
    default:
        return NULL;
    }
}

static schedule_slot_t *schedule_config_get_slot_mut(schedule_config_t *cfg,
                                                     schedule_actuator_t act,
                                                     uint32_t index)
{
    switch (act) {
    case SCHEDULE_ACTUATOR_HEATING:
        return &cfg->heating[index];
    case SCHEDULE_ACTUATOR_UV:
        return &cfg->uv[index];
    case SCHEDULE_ACTUATOR_LIGHTING:
        return &cfg->lighting[index];
    case SCHEDULE_ACTUATOR_VENTILATION:
        return &cfg->ventilation[index];
    default:
        return NULL;
    }
}

static void schedule_screen_populate(const schedule_config_t *cfg)
{
    for (uint32_t act = 0; act < SCHEDULE_ACTUATOR_COUNT; ++act) {
        for (uint32_t slot = 0; slot < SCHEDULE_SLOTS_PER_ACTUATOR; ++slot) {
            const schedule_slot_t *s = schedule_config_get_slot(cfg, (schedule_actuator_t)act, slot);
            if (!s)
                continue;
            if (s->enabled) {
                lv_obj_add_state(slot_switch[act][slot], LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(slot_switch[act][slot], LV_STATE_CHECKED);
            }
            uint16_t start = s->start_minute % 1440;
            uint16_t end = s->end_minute % 1440;
            lv_spinbox_set_value(slot_start_hour[act][slot], start / 60);
            lv_spinbox_set_value(slot_start_min[act][slot], start % 60);
            lv_spinbox_set_value(slot_end_hour[act][slot], end / 60);
            lv_spinbox_set_value(slot_end_min[act][slot], end % 60);
        }
    }
}

static void schedule_screen_collect(schedule_config_t *cfg)
{
    for (uint32_t act = 0; act < SCHEDULE_ACTUATOR_COUNT; ++act) {
        for (uint32_t slot = 0; slot < SCHEDULE_SLOTS_PER_ACTUATOR; ++slot) {
            schedule_slot_t *s = schedule_config_get_slot_mut(cfg, (schedule_actuator_t)act, slot);
            if (!s)
                continue;
            s->enabled = lv_obj_has_state(slot_switch[act][slot], LV_STATE_CHECKED);
            uint16_t start_hour = (uint16_t)lv_spinbox_get_value(slot_start_hour[act][slot]);
            uint16_t start_min = (uint16_t)lv_spinbox_get_value(slot_start_min[act][slot]);
            uint16_t end_hour = (uint16_t)lv_spinbox_get_value(slot_end_hour[act][slot]);
            uint16_t end_min = (uint16_t)lv_spinbox_get_value(slot_end_min[act][slot]);
            s->start_minute = (start_hour % 24) * 60 + (start_min % 60);
            s->end_minute = (end_hour % 24) * 60 + (end_min % 60);
        }
    }
}

static void schedule_screen_show(void)
{
    if (!schedule_screen) {
        schedule_screen = lv_obj_create(NULL);
        lv_obj_set_style_pad_all(schedule_screen, 16, 0);
        lv_obj_set_style_pad_gap(schedule_screen, 12, 0);
        lv_obj_set_flex_flow(schedule_screen, LV_FLEX_FLOW_COLUMN);

        for (uint32_t act = 0; act < SCHEDULE_ACTUATOR_COUNT; ++act) {
            lv_obj_t *act_cont = lv_obj_create(schedule_screen);
            lv_obj_remove_style_all(act_cont);
            lv_obj_set_style_pad_all(act_cont, 0, 0);
            lv_obj_set_style_pad_gap(act_cont, 8, 0);
            lv_obj_set_size(act_cont, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(act_cont, LV_FLEX_FLOW_COLUMN);

            lv_obj_t *title = lv_label_create(act_cont);
            lv_label_set_text(title, schedule_actuator_name((schedule_actuator_t)act));

            for (uint32_t slot = 0; slot < SCHEDULE_SLOTS_PER_ACTUATOR; ++slot) {
                lv_obj_t *row = lv_obj_create(act_cont);
                lv_obj_remove_style_all(row);
                lv_obj_set_style_pad_all(row, 0, 0);
                lv_obj_set_style_pad_gap(row, 6, 0);
                lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row,
                                      LV_FLEX_ALIGN_START,
                                      LV_FLEX_ALIGN_CENTER,
                                      LV_FLEX_ALIGN_CENTER);

                slot_switch[act][slot] = lv_switch_create(row);

                lv_obj_t *slot_label = lv_label_create(row);
                lv_label_set_text_fmt(slot_label, "Plage %u", (unsigned)(slot + 1));
                lv_obj_set_flex_grow(slot_label, 1);
                lv_obj_set_style_pad_left(slot_label, 6, 0);

                slot_start_hour[act][slot] = create_hour_spinbox(row);
                lv_obj_t *colon1 = lv_label_create(row);
                lv_label_set_text(colon1, ":");
                slot_start_min[act][slot] = create_minute_spinbox(row);

                lv_obj_t *arrow = lv_label_create(row);
                lv_label_set_text(arrow, LV_SYMBOL_RIGHT);

                slot_end_hour[act][slot] = create_hour_spinbox(row);
                lv_obj_t *colon2 = lv_label_create(row);
                lv_label_set_text(colon2, ":");
                slot_end_min[act][slot] = create_minute_spinbox(row);
            }
        }

        lv_obj_t *btn_row = lv_obj_create(schedule_screen);
        lv_obj_remove_style_all(btn_row);
        lv_obj_set_style_pad_all(btn_row, 0, 0);
        lv_obj_set_style_pad_gap(btn_row, 12, 0);
        lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);

        lv_obj_t *btn_cancel = lv_btn_create(btn_row);
        lv_obj_set_flex_grow(btn_cancel, 1);
        lv_obj_add_event_cb(btn_cancel, schedule_cancel_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
        lv_label_set_text(lbl_cancel, "Annuler");
        lv_obj_center(lbl_cancel);

        lv_obj_t *btn_save = lv_btn_create(btn_row);
        lv_obj_set_flex_grow(btn_save, 1);
        lv_obj_add_event_cb(btn_save, schedule_save_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl_save = lv_label_create(btn_save);
        lv_label_set_text(lbl_save, "Enregistrer");
        lv_obj_center(lbl_save);
    }

    schedule_config_t cfg;
    schedule_get_config(&cfg);
    schedule_screen_populate(&cfg);
    lv_scr_load(schedule_screen);
}

static void schedule_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (schedule_screen) {
        lv_scr_load(screen);
        lv_obj_del(schedule_screen);
        schedule_screen = NULL;
        update_status_labels();
    }
}

static void schedule_save_cb(lv_event_t *e)
{
    (void)e;
    schedule_config_t cfg;
    schedule_get_config(&cfg);
    schedule_screen_collect(&cfg);
    esp_err_t err = schedule_set_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Échec sauvegarde planning: %s", esp_err_to_name(err));
    }
    enforce_schedule_now();
    if (schedule_screen) {
        lv_scr_load(screen);
        lv_obj_del(schedule_screen);
        schedule_screen = NULL;
    }
    update_status_labels();
}

static void schedule_btn_cb(lv_event_t *e)
{
    (void)e;
    schedule_screen_show();
}

static bool enforce_schedule_now(void)
{
    schedule_state_t st;
    if (!schedule_get_current_state(&st)) {
        return false;
    }
    bool changed = false;
    if (st.heating != heating_allowed) {
        heating_allowed = st.heating;
        reptile_env_set_heating_allowed(heating_allowed);
        changed = true;
    }
    if (st.ventilation != fan_on) {
        reptile_fan_set(st.ventilation);
        fan_on = reptile_fan_get();
        changed = true;
    }
    if (st.uv != uv_lamp_on) {
        reptile_uv_lamp_set(st.uv);
        uv_lamp_on = reptile_uv_lamp_get();
        changed = true;
    }
    if (st.lighting != light_on) {
        reptile_light_set(st.lighting);
        light_on = reptile_light_get();
        changed = true;
    }
    return changed;
}

static bool logging_real_sample(logging_real_sample_t *sample)
{
    if (!sample) {
        return false;
    }
    sample->temperature_c = s_last_temp;
    sample->humidity_pct = s_last_hum;
    sample->lux = s_last_lux;
    sample->uva_mw_cm2 = s_last_uv.uva;
    sample->uvb_mw_cm2 = s_last_uv.uvb;
    sample->uv_index = s_last_uv.uv_index;
    reptile_env_state_t env;
    reptile_env_get_state(&env);
    sample->heating = env.heating;
    sample->pumping = env.pumping;
    sample->fan = fan_on;
    sample->uv_lamp = uv_lamp_on;
    sample->light = light_on;
    sample->feeding = feed_running;
    return true;
}

static void update_status_labels(void)
{
    if (!label_temp || !label_hum) {
        return;
    }

    if (isnan(s_env_state.temperature)) {
        lv_label_set_text(label_temp, "Température: Non connecté");
    } else {
        lv_label_set_text_fmt(label_temp, "Température: %.1f °C", s_env_state.temperature);
    }

    if (isnan(s_env_state.humidity)) {
        lv_label_set_text(label_hum, "Humidité: Non connecté");
    } else {
        lv_label_set_text_fmt(label_hum, "Humidité: %.1f %%", s_env_state.humidity);
    }

    if (label_pump) {
        lv_label_set_text(label_pump, s_env_state.pumping ? "Pompe: ON" : "Pompe: OFF");
    }
    if (label_heat) {
        lv_label_set_text(label_heat, s_env_state.heating ? "Chauffage: ON" : "Chauffage: OFF");
    }
    if (label_feed) {
        lv_label_set_text(label_feed, feed_running ? "Nourrissage: ON" : "Nourrissage: OFF");
    }
    if (label_fan) {
        lv_label_set_text(label_fan, fan_on ? "Ventilateur: ON" : "Ventilateur: OFF");
    }
    if (label_uv_lamp) {
        lv_label_set_text(label_uv_lamp, uv_lamp_on ? "Lampe UV: ON" : "Lampe UV: OFF");
    }
    if (label_light) {
        lv_label_set_text(label_light, light_on ? "Éclairage: ON" : "Éclairage: OFF");
    }
}

static void update_sensor_widgets(float temp, float hum, float lux, sensor_uv_data_t uv)
{
    if (chart) {
        if (chart_series_temp) {
            if (isnan(temp)) {
                lv_chart_set_next_value(chart, chart_series_temp, LV_CHART_POINT_NONE);
            } else {
                float clamped = fminf(fmaxf(temp, TEMP_MIN_DISPLAY), TEMP_MAX_DISPLAY);
                lv_chart_set_next_value(chart, chart_series_temp, (lv_coord_t)clamped);
            }
        }
        if (chart_series_hum) {
            if (isnan(hum)) {
                lv_chart_set_next_value(chart, chart_series_hum, LV_CHART_POINT_NONE);
            } else {
                float clamped = fminf(fmaxf(hum, HUMIDITY_MIN_DISPLAY), HUMIDITY_MAX_DISPLAY);
                lv_chart_set_next_value(chart, chart_series_hum, (lv_coord_t)clamped);
            }
        }
        lv_chart_refresh(chart);
    }

    if (label_lux) {
        if (isnan(lux)) {
            lv_label_set_text(label_lux, "Luminosité: Non connectée");
        } else {
            lv_label_set_text_fmt(label_lux, "Luminosité: %.0f lx", lux);
        }
    }

    if (label_uv) {
        if (isnan(uv.uv_index)) {
            lv_label_set_text(label_uv, "UV: Non connectés");
        } else {
            float uva = isnan(uv.uva) ? 0.0f : uv.uva;
            float uvb = isnan(uv.uvb) ? 0.0f : uv.uvb;
            lv_label_set_text_fmt(label_uv,
                                  "UV index: %.2f (UVA %.3f / UVB %.3f)",
                                  uv.uv_index,
                                  uva,
                                  uvb);
        }
    }

    if (sensor_meter) {
        if (needle_lux) {
            lv_coord_t lux_value = 0;
            if (!isnan(lux)) {
                float clamped = fminf(fmaxf(lux, 0.0f), LUX_METER_MAX);
                lux_value = (lv_coord_t)clamped;
            }
            lv_meter_set_indicator_value(sensor_meter, needle_lux, lux_value);
        }
        if (needle_uv) {
            lv_coord_t uv_value = 0;
            if (!isnan(uv.uv_index)) {
                float clamped = fminf(fmaxf(uv.uv_index, 0.0f), UV_INDEX_METER_MAX);
                uv_value = (lv_coord_t)clamped;
            }
            lv_meter_set_indicator_value(sensor_meter, needle_uv, uv_value);
        }
    }
}

static void env_state_cb(const reptile_env_state_t *state, void *ctx)
{
    (void)ctx;
    s_env_state = *state;
    if (lvgl_port_lock(-1)) {
        update_status_labels();
        lvgl_port_unlock();
    }
}

static void feed_task(void *arg)
{
    (void)arg;
    feed_running = true;
    if (lvgl_port_lock(-1)) {
        update_status_labels();
        lvgl_port_unlock();
    }
    reptile_feed_gpio();
    feed_running = false;
    if (lvgl_port_lock(-1)) {
        update_status_labels();
        lvgl_port_unlock();
    }
    feed_task_handle = NULL;
    vTaskDelete(NULL);
}

static void pump_btn_cb(lv_event_t *e)
{
    (void)e;
    reptile_env_manual_pump();
}

static void heat_btn_cb(lv_event_t *e)
{
    (void)e;
    reptile_env_manual_heat();
}

static void feed_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!feed_running) {
        xTaskCreate(feed_task, "feed_task", 2048, NULL, 5, &feed_task_handle);
    }
}

static void fan_btn_cb(lv_event_t *e)
{
    (void)e;
    fan_on = !fan_on;
    reptile_fan_set(fan_on);
    fan_on = reptile_fan_get();
    update_status_labels();
}

static void uv_btn_cb(lv_event_t *e)
{
    (void)e;
    uv_lamp_on = !uv_lamp_on;
    reptile_uv_lamp_set(uv_lamp_on);
    uv_lamp_on = reptile_uv_lamp_get();
    update_status_labels();
}

static void light_btn_cb(lv_event_t *e)
{
    (void)e;
    light_on = !light_on;
    reptile_light_set(light_on);
    light_on = reptile_light_get();
    update_status_labels();
}

static void sensor_poll_task(void *arg)
{
    (void)arg;
    const TickType_t delay = pdMS_TO_TICKS(SENSOR_POLL_PERIOD_MS);

    while (sensor_task_running) {
        float temp = sensors_read_temperature();
        float hum = sensors_read_humidity();
        float lux = sensors_read_lux();
        sensor_uv_data_t uv = sensors_read_uv();

        s_last_temp = temp;
        s_last_hum = hum;
        s_last_lux = lux;
        s_last_uv = uv;

        bool schedule_changed = enforce_schedule_now();

        if (!sensor_task_running) {
            break;
        }

        if (lvgl_port_lock(-1)) {
            if (sensor_ui_ready) {
                update_sensor_widgets(temp, hum, lux, uv);
            }
            if (schedule_changed) {
                update_status_labels();
            }
            lvgl_port_unlock();
        }

        if (!sensor_task_running) {
            break;
        }

        (void)ulTaskNotifyTake(pdTRUE, delay);
    }

    sensor_task_running = false;
    if (sensor_task_waiter) {
        xTaskNotifyGive(sensor_task_waiter);
        sensor_task_waiter = NULL;
    }
    sensor_task_handle = NULL;
    vTaskDelete(NULL);
}

static void menu_btn_cb(lv_event_t *e)
{
    (void)e;

    if (schedule_screen) {
        lv_obj_del(schedule_screen);
        schedule_screen = NULL;
    }

    sensor_ui_ready = false;
    if (sensor_task_handle) {
        sensor_task_waiter = xTaskGetCurrentTaskHandle();
        sensor_task_running = false;
        xTaskNotifyGive(sensor_task_handle);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SENSOR_POLL_PERIOD_MS + 200));
        sensor_task_waiter = NULL;
    }

    reptile_env_stop();
    sensors_deinit();

    if (feed_task_handle) {
        vTaskDelete(feed_task_handle);
        feed_task_handle = NULL;
    }
    feed_running = false;

    reptile_fan_set(false);
    reptile_uv_lamp_set(false);
    reptile_light_set(false);
    fan_on = false;
    uv_lamp_on = false;
    light_on = false;

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

    esp_err_t sched_err = schedule_init();
    if (sched_err != ESP_OK) {
        ESP_LOGE(TAG, "Échec init planning: %s", esp_err_to_name(sched_err));
    }

    if (!lvgl_port_lock(-1)) {
        return;
    }

    s_last_temp = NAN;
    s_last_hum = NAN;
    s_last_lux = NAN;
    s_last_uv.uva = NAN;
    s_last_uv.uvb = NAN;
    s_last_uv.uv_index = NAN;

    screen = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(screen, 12, 0);
    lv_obj_set_style_pad_gap(screen, 12, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *status_cont = lv_obj_create(screen);
    lv_obj_remove_style_all(status_cont);
    lv_obj_set_size(status_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(status_cont, 6, 0);
    lv_obj_set_style_pad_all(status_cont, 0, 0);

    label_temp = lv_label_create(status_cont);
    lv_label_set_text(label_temp, "Température: Non connecté");
    label_hum = lv_label_create(status_cont);
    lv_label_set_text(label_hum, "Humidité: Non connecté");
    label_lux = lv_label_create(status_cont);
    lv_label_set_text(label_lux, "Luminosité: Non connectée");
    label_uv = lv_label_create(status_cont);
    lv_label_set_text(label_uv, "UV: Non connectés");

    create_action_row(status_cont, &label_pump, "Pompe: OFF", "Pompe", pump_btn_cb);
    create_action_row(status_cont, &label_heat, "Chauffage: OFF", "Chauffage", heat_btn_cb);
    create_action_row(status_cont, &label_feed, "Nourrissage: OFF", "Nourrir", feed_btn_cb);
    create_action_row(status_cont, &label_fan, "Ventilateur: OFF", "Ventilateur", fan_btn_cb);
    create_action_row(status_cont, &label_uv_lamp, "Lampe UV: OFF", "Lampe UV", uv_btn_cb);
    create_action_row(status_cont, &label_light, "Éclairage: OFF", "Éclairage", light_btn_cb);

    lv_obj_t *data_cont = lv_obj_create(screen);
    lv_obj_remove_style_all(data_cont);
    lv_obj_set_size(data_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(data_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(data_cont, 12, 0);
    lv_obj_set_style_pad_all(data_cont, 0, 0);

    lv_obj_t *chart_cont = lv_obj_create(data_cont);
    lv_obj_remove_style_all(chart_cont);
    lv_obj_set_flex_flow(chart_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(chart_cont, 8, 0);
    lv_obj_set_style_pad_all(chart_cont, 0, 0);
    lv_obj_set_flex_grow(chart_cont, 1);

    lv_obj_t *chart_title = lv_label_create(chart_cont);
    lv_label_set_text(chart_title, "Température & Humidité");

    chart = lv_chart_create(chart_cont);
    lv_obj_set_height(chart, 160);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart, CHART_POINT_COUNT);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, (lv_coord_t)TEMP_MIN_DISPLAY, (lv_coord_t)HUMIDITY_MAX_DISPLAY);
    chart_series_temp = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    chart_series_hum = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart, chart_series_temp, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(chart, chart_series_hum, LV_CHART_POINT_NONE);

    lv_obj_t *meter_cont = lv_obj_create(data_cont);
    lv_obj_remove_style_all(meter_cont);
    lv_obj_set_flex_flow(meter_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(meter_cont, 8, 0);
    lv_obj_set_style_pad_all(meter_cont, 0, 0);
    lv_obj_set_flex_grow(meter_cont, 1);

    lv_obj_t *meter_title = lv_label_create(meter_cont);
    lv_label_set_text(meter_title, "Luminosité & UV");

    sensor_meter = lv_meter_create(meter_cont);
    lv_obj_set_size(sensor_meter, 200, 200);

    meter_scale_lux = lv_meter_add_scale(sensor_meter);
    lv_meter_set_scale_ticks(sensor_meter, meter_scale_lux, 21, 2, 10, lv_palette_main(LV_PALETTE_AMBER));
    lv_meter_set_scale_major_ticks(sensor_meter, meter_scale_lux, 4, 4, 15, lv_palette_main(LV_PALETTE_AMBER), 10);
    lv_meter_set_scale_range(sensor_meter, meter_scale_lux, 0, (lv_coord_t)LUX_METER_MAX, 270, 90);
    needle_lux = lv_meter_add_needle_line(sensor_meter, meter_scale_lux, 6, lv_palette_main(LV_PALETTE_AMBER), -12);

    meter_scale_uv = lv_meter_add_scale(sensor_meter);
    lv_meter_set_scale_ticks(sensor_meter, meter_scale_uv, 13, 2, 10, lv_palette_main(LV_PALETTE_PURPLE));
    lv_meter_set_scale_major_ticks(sensor_meter, meter_scale_uv, 4, 4, 15, lv_palette_main(LV_PALETTE_PURPLE), 10);
    lv_meter_set_scale_range(sensor_meter, meter_scale_uv, 0, (lv_coord_t)UV_INDEX_METER_MAX, 270, 90);
    needle_uv = lv_meter_add_needle_line(sensor_meter, meter_scale_uv, 4, lv_palette_main(LV_PALETTE_PURPLE), 12);

    lv_obj_t *footer = lv_obj_create(screen);
    lv_obj_remove_style_all(footer);
    lv_obj_set_size(footer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_set_style_pad_gap(footer, 12, 0);

    lv_obj_t *btn_schedule = lv_btn_create(footer);
    lv_obj_set_flex_grow(btn_schedule, 1);
    lv_obj_add_event_cb(btn_schedule, schedule_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_schedule = lv_label_create(btn_schedule);
    lv_label_set_text(lbl_schedule, "Planning");
    lv_obj_center(lbl_schedule);

    lv_obj_t *btn_menu = lv_btn_create(footer);
    lv_obj_set_flex_grow(btn_menu, 1);
    lv_obj_add_event_cb(btn_menu, menu_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_menu = lv_label_create(btn_menu);
    lv_label_set_text(lbl_menu, "Menu");
    lv_obj_center(lbl_menu);

    s_env_state.temperature = NAN;
    s_env_state.humidity = NAN;
    s_env_state.heating = false;
    s_env_state.pumping = false;
    feed_running = false;

    fan_on = reptile_fan_get();
    uv_lamp_on = reptile_uv_lamp_get();
    light_on = reptile_light_get();

    update_status_labels();
    sensor_uv_data_t uv_init = {
        .uva = NAN,
        .uvb = NAN,
        .uv_index = NAN,
    };
    update_sensor_widgets(NAN, NAN, NAN, uv_init);

    const logging_provider_t log_provider = {
        .get_real_sample = logging_real_sample,
        .period_ms = 60000,
    };
    logging_init(&log_provider);

    lv_disp_load_scr(screen);
    lvgl_port_unlock();

    enforce_schedule_now();
    if (lvgl_port_lock(-1)) {
        update_status_labels();
        lvgl_port_unlock();
    }

    reptile_env_thresholds_t thr = {
        .temp_setpoint = g_settings.temp_threshold,
        .humidity_setpoint = g_settings.humidity_threshold,
    };
    reptile_env_start(&thr, env_state_cb, NULL);

    if (enforce_schedule_now() && lvgl_port_lock(-1)) {
        update_status_labels();
        lvgl_port_unlock();
    }

    sensor_task_waiter = NULL;
    sensor_ui_ready = true;
    sensor_task_running = true;
    if (xTaskCreate(sensor_poll_task,
                    "sensor_poll",
                    SENSOR_TASK_STACK_SIZE,
                    NULL,
                    SENSOR_TASK_PRIORITY,
                    &sensor_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start sensor polling task");
        sensor_task_handle = NULL;
        sensor_task_running = false;
    }
}
