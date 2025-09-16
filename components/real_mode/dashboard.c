#include <stddef.h>
#include <stdio.h>
#include <math.h>
#include "dashboard.h"
#include "actuators.h"
#include "esp_log.h"

static const char *TAG = "dashboard";

static lv_obj_t *scr;
static lv_obj_t *lbl_temp;
static lv_obj_t *lbl_hum;
static lv_obj_t *lbl_lux;
static lv_obj_t *lbl_co2;
static lv_obj_t *sw_mode;
static lv_obj_t *sw_heater;
static lv_obj_t *sw_uv;
static lv_obj_t *sw_neon;
static lv_obj_t *sw_pump;
static lv_obj_t *sw_fan;
static lv_obj_t *sw_humid;
static lv_obj_t *lbl_heater_status;
static lv_obj_t *lbl_uv_status;
static lv_obj_t *lbl_neon_status;
static lv_obj_t *lbl_pump_status;
static lv_obj_t *lbl_fan_status;
static lv_obj_t *lbl_humid_status;

enum {
    ACT_WIDGET_HEATER,
    ACT_WIDGET_UV,
    ACT_WIDGET_NEON,
    ACT_WIDGET_PUMP,
    ACT_WIDGET_FAN,
    ACT_WIDGET_HUMID,
};

static sensor_data_t s_last_data;
static bool s_has_data = false;
static bool s_colors_ready = false;
static lv_color_t s_sensor_color;
static lv_color_t s_actuator_color;
static lv_color_t s_disabled_color;

extern terrarium_device_status_t g_device_status[];
extern const size_t g_terrarium_count;
extern real_mode_state_t g_real_mode_state[];
extern terrarium_hw_t g_terrariums[];

static void ensure_colors_ready(void)
{
    if (!s_colors_ready && lbl_temp && lbl_heater_status) {
        s_sensor_color = lv_obj_get_style_text_color(lbl_temp, LV_PART_MAIN);
        s_actuator_color = lv_obj_get_style_text_color(lbl_heater_status, LV_PART_MAIN);
        s_disabled_color = lv_palette_main(LV_PALETTE_GREY);
        s_colors_ready = true;
    }
}

static const terrarium_device_status_t *get_status(void)
{
    if (g_terrarium_count == 0) {
        return NULL;
    }
    return &g_device_status[0];
}

static void update_sensor_labels(void)
{
    ensure_colors_ready();
    const terrarium_device_status_t *status = get_status();
    bool temp_connected = status && status->sensors.temperature_humidity;
    bool lux_connected = status && status->sensors.luminosity;
    bool co2_connected = status && status->sensors.co2;
    char buf[64];

    if (!temp_connected) {
        lv_label_set_text(lbl_temp, "Temp: Non connecté");
        lv_label_set_text(lbl_hum, "Hum: Non connecté");
        lv_obj_set_style_text_color(lbl_temp, s_disabled_color, 0);
        lv_obj_set_style_text_color(lbl_hum, s_disabled_color, 0);
    } else {
        if (s_has_data && !isnan(s_last_data.temperature_c)) {
            snprintf(buf, sizeof(buf), "Temp: %.1f C", s_last_data.temperature_c);
        } else {
            snprintf(buf, sizeof(buf), "Temp: --");
        }
        lv_label_set_text(lbl_temp, buf);
        if (s_has_data && !isnan(s_last_data.humidity_pct)) {
            snprintf(buf, sizeof(buf), "Hum: %.1f %%", s_last_data.humidity_pct);
        } else {
            snprintf(buf, sizeof(buf), "Hum: --");
        }
        lv_label_set_text(lbl_hum, buf);
        lv_obj_set_style_text_color(lbl_temp, s_sensor_color, 0);
        lv_obj_set_style_text_color(lbl_hum, s_sensor_color, 0);
    }

    if (!lux_connected) {
        lv_label_set_text(lbl_lux, "Lum: Non connecté");
        lv_obj_set_style_text_color(lbl_lux, s_disabled_color, 0);
    } else {
        if (s_has_data && !isnan(s_last_data.luminosity_lux)) {
            snprintf(buf, sizeof(buf), "Lum: %.1f lx", s_last_data.luminosity_lux);
        } else {
            snprintf(buf, sizeof(buf), "Lum: --");
        }
        lv_label_set_text(lbl_lux, buf);
        lv_obj_set_style_text_color(lbl_lux, s_sensor_color, 0);
    }

    if (!co2_connected) {
        lv_label_set_text(lbl_co2, "CO2: Non connecté");
        lv_obj_set_style_text_color(lbl_co2, s_disabled_color, 0);
    } else {
        if (s_has_data && !isnan(s_last_data.co2_ppm)) {
            snprintf(buf, sizeof(buf), "CO2: %.1f ppm", s_last_data.co2_ppm);
        } else {
            snprintf(buf, sizeof(buf), "CO2: --");
        }
        lv_label_set_text(lbl_co2, buf);
        lv_obj_set_style_text_color(lbl_co2, s_sensor_color, 0);
    }
}

static void update_actuator_controls(void)
{
    ensure_colors_ready();
    const terrarium_device_status_t *status = get_status();
    const actuator_connection_t *act = status ? &status->actuators : NULL;
    real_mode_state_t *st = &g_real_mode_state[0];
    bool manual = st->manual_mode;

    struct {
        lv_obj_t *sw;
        lv_obj_t *label;
        const char *name;
        bool available;
        bool *state;
    } widgets[] = {
        {sw_heater, lbl_heater_status, "Chauffage", act && act->heater, &st->actuators.heater},
        {sw_uv,     lbl_uv_status,     "UV",        act && act->uv,      &st->actuators.uv},
        {sw_neon,   lbl_neon_status,   "Néon",      act && act->neon,    &st->actuators.neon},
        {sw_pump,   lbl_pump_status,   "Pompe",     act && act->pump,    &st->actuators.pump},
        {sw_fan,    lbl_fan_status,    "Ventilation", act && act->fan,   &st->actuators.fan},
        {sw_humid,  lbl_humid_status,  "Humidificateur", act && act->humidifier, &st->actuators.humidifier},
    };

    for (size_t i = 0; i < sizeof(widgets)/sizeof(widgets[0]); ++i) {
        char label_text[64];
        if (!widgets[i].available) {
            snprintf(label_text, sizeof(label_text), "%s (Non connecté)", widgets[i].name);
            *(widgets[i].state) = false;
            lv_obj_add_flag(widgets[i].sw, LV_OBJ_FLAG_DISABLED);
            lv_obj_clear_state(widgets[i].sw, LV_STATE_CHECKED);
            lv_obj_set_style_text_color(widgets[i].label, s_disabled_color, 0);
        } else {
            snprintf(label_text, sizeof(label_text), "%s", widgets[i].name);
            if (manual) {
                lv_obj_clear_flag(widgets[i].sw, LV_OBJ_FLAG_DISABLED);
            } else {
                lv_obj_add_flag(widgets[i].sw, LV_OBJ_FLAG_DISABLED);
            }
            if (*(widgets[i].state)) {
                lv_obj_add_state(widgets[i].sw, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(widgets[i].sw, LV_STATE_CHECKED);
            }
            lv_obj_set_style_text_color(widgets[i].label, s_actuator_color, 0);
        }
        lv_label_set_text(widgets[i].label, label_text);
    }
}

static void actuator_cb(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    real_mode_state_t *st = &g_real_mode_state[0];
    const terrarium_device_status_t *status = get_status();
    if (!status) {
        return;
    }

    const actuator_connection_t *act = &status->actuators;
    bool available = false;

    switch (id) {
        case ACT_WIDGET_HEATER:
            available = act->heater;
            if (available) st->actuators.heater = on;
            break;
        case ACT_WIDGET_UV:
            available = act->uv;
            if (available) st->actuators.uv = on;
            break;
        case ACT_WIDGET_NEON:
            available = act->neon;
            if (available) st->actuators.neon = on;
            break;
        case ACT_WIDGET_PUMP:
            available = act->pump;
            if (available) st->actuators.pump = on;
            break;
        case ACT_WIDGET_FAN:
            available = act->fan;
            if (available) st->actuators.fan = on;
            break;
        case ACT_WIDGET_HUMID:
            available = act->humidifier;
            if (available) st->actuators.humidifier = on;
            break;
        default: break;
    }

    if (!available) {
        return;
    }

    actuators_apply(&g_terrariums[0], NULL, st);
}

static void mode_cb(lv_event_t *e)
{
    bool manual = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    real_mode_state_t *st = &g_real_mode_state[0];
    st->manual_mode = manual;
    update_actuator_controls();
    if (manual) {
        actuators_apply(&g_terrariums[0], NULL, st);
    }
}

static void create_actuator_label(lv_obj_t **label, lv_obj_t *ref, lv_coord_t x_ofs, lv_coord_t y_ofs, const char *text)
{
    *label = lv_label_create(scr);
    lv_label_set_text(*label, text);
    lv_obj_align_to(*label, ref, LV_ALIGN_OUT_RIGHT_MID, x_ofs, y_ofs);
}

void dashboard_init(void)
{
    scr = lv_obj_create(NULL);
    lbl_temp = lv_label_create(scr);
    lv_obj_align(lbl_temp, LV_ALIGN_TOP_LEFT, 0, 0);
    lbl_hum = lv_label_create(scr);
    lv_obj_align(lbl_hum, LV_ALIGN_TOP_LEFT, 0, 20);
    lbl_lux = lv_label_create(scr);
    lv_obj_align(lbl_lux, LV_ALIGN_TOP_LEFT, 0, 40);
    lbl_co2 = lv_label_create(scr);
    lv_obj_align(lbl_co2, LV_ALIGN_TOP_LEFT, 0, 60);

    sw_mode = lv_switch_create(scr);
    lv_obj_align(sw_mode, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(sw_mode, mode_cb, LV_EVENT_VALUE_CHANGED, NULL);

    sw_heater = lv_switch_create(scr);
    lv_obj_align(sw_heater, LV_ALIGN_TOP_LEFT, 120, 0);
    lv_obj_add_event_cb(sw_heater, actuator_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)ACT_WIDGET_HEATER);

    sw_uv = lv_switch_create(scr);
    lv_obj_align(sw_uv, LV_ALIGN_TOP_LEFT, 120, 20);
    lv_obj_add_event_cb(sw_uv, actuator_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)ACT_WIDGET_UV);

    sw_neon = lv_switch_create(scr);
    lv_obj_align(sw_neon, LV_ALIGN_TOP_LEFT, 120, 40);
    lv_obj_add_event_cb(sw_neon, actuator_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)ACT_WIDGET_NEON);

    sw_pump = lv_switch_create(scr);
    lv_obj_align(sw_pump, LV_ALIGN_TOP_LEFT, 120, 60);
    lv_obj_add_event_cb(sw_pump, actuator_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)ACT_WIDGET_PUMP);

    sw_fan = lv_switch_create(scr);
    lv_obj_align(sw_fan, LV_ALIGN_TOP_LEFT, 120, 80);
    lv_obj_add_event_cb(sw_fan, actuator_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)ACT_WIDGET_FAN);

    sw_humid = lv_switch_create(scr);
    lv_obj_align(sw_humid, LV_ALIGN_TOP_LEFT, 120, 100);
    lv_obj_add_event_cb(sw_humid, actuator_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)ACT_WIDGET_HUMID);

    create_actuator_label(&lbl_heater_status, sw_heater, 10, 0, "Chauffage");
    create_actuator_label(&lbl_uv_status, sw_uv, 10, 0, "UV");
    create_actuator_label(&lbl_neon_status, sw_neon, 10, 0, "Néon");
    create_actuator_label(&lbl_pump_status, sw_pump, 10, 0, "Pompe");
    create_actuator_label(&lbl_fan_status, sw_fan, 10, 0, "Ventilation");
    create_actuator_label(&lbl_humid_status, sw_humid, 10, 0, "Humidificateur");

    ensure_colors_ready();
    update_sensor_labels();
    update_actuator_controls();
}

void dashboard_show(void)
{
    if (!scr) {
        dashboard_init();
    }
    lv_scr_load(scr);
}

void dashboard_update(const sensor_data_t *data)
{
    /*
     * IMPORTANT: cette fonction manipule des objets LVGL. Tout appel depuis un
     * autre task que celui exécutant lv_timer_handler() doit être effectué sous
     * protection lvgl_port_lock()/lvgl_port_unlock().
     */
    if (data) {
        s_last_data = *data;
        s_has_data = true;
    }
    update_sensor_labels();
}

void dashboard_set_device_status(size_t terrarium_idx, const terrarium_device_status_t *status)
{
    /* Voir la remarque sur dashboard_update() concernant le verrou LVGL. */
    if (terrarium_idx != 0 || !status) {
        return;
    }
    (void)status;
    update_sensor_labels();
    update_actuator_controls();
}
