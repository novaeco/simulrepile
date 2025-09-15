#include <stddef.h>
#include <stdio.h>
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

enum {
    ACT_HEATER,
    ACT_UV,
    ACT_NEON,
    ACT_PUMP,
    ACT_FAN,
    ACT_HUMID
};

static void actuator_cb(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    real_mode_state_t *st = &g_real_mode_state[0];
    switch (id) {
        case ACT_HEATER: st->actuators.heater = on; break;
        case ACT_UV: st->actuators.uv = on; break;
        case ACT_NEON: st->actuators.neon = on; break;
        case ACT_PUMP: st->actuators.pump = on; break;
        case ACT_FAN: st->actuators.fan = on; break;
        case ACT_HUMID: st->actuators.humidifier = on; break;
    }
    actuators_apply(&g_terrariums[0], NULL, st);
}

static void mode_cb(lv_event_t *e)
{
    bool manual = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    real_mode_state_t *st = &g_real_mode_state[0];
    st->manual_mode = manual;
    lv_obj_t *sw_list[] = {sw_heater, sw_uv, sw_neon, sw_pump, sw_fan, sw_humid};
    for (size_t i = 0; i < sizeof(sw_list)/sizeof(sw_list[0]); ++i) {
        if (manual) {
            lv_obj_clear_flag(sw_list[i], LV_OBJ_FLAG_DISABLED);
        } else {
            lv_obj_add_flag(sw_list[i], LV_OBJ_FLAG_DISABLED);
        }
    }
    if (manual) {
        actuators_apply(&g_terrariums[0], NULL, st);
    }
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
    lv_obj_add_event_cb(sw_heater, actuator_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)ACT_HEATER);

    sw_uv = lv_switch_create(scr);
    lv_obj_align(sw_uv, LV_ALIGN_TOP_LEFT, 120, 20);
    lv_obj_add_event_cb(sw_uv, actuator_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)ACT_UV);

    sw_neon = lv_switch_create(scr);
    lv_obj_align(sw_neon, LV_ALIGN_TOP_LEFT, 120, 40);
    lv_obj_add_event_cb(sw_neon, actuator_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)ACT_NEON);

    sw_pump = lv_switch_create(scr);
    lv_obj_align(sw_pump, LV_ALIGN_TOP_LEFT, 120, 60);
    lv_obj_add_event_cb(sw_pump, actuator_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)ACT_PUMP);

    sw_fan = lv_switch_create(scr);
    lv_obj_align(sw_fan, LV_ALIGN_TOP_LEFT, 120, 80);
    lv_obj_add_event_cb(sw_fan, actuator_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)ACT_FAN);

    sw_humid = lv_switch_create(scr);
    lv_obj_align(sw_humid, LV_ALIGN_TOP_LEFT, 120, 100);
    lv_obj_add_event_cb(sw_humid, actuator_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)ACT_HUMID);

    lv_event_send(sw_mode, LV_EVENT_VALUE_CHANGED, NULL);
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
    if (!data) {
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "Temp: %.1f C", data->temperature_c);
    lv_label_set_text(lbl_temp, buf);
    snprintf(buf, sizeof(buf), "Hum: %.1f %%", data->humidity_pct);
    lv_label_set_text(lbl_hum, buf);
    snprintf(buf, sizeof(buf), "Lum: %.1f lx", data->luminosity_lux);
    lv_label_set_text(lbl_lux, buf);
    snprintf(buf, sizeof(buf), "CO2: %.1f ppm", data->co2_ppm);
    lv_label_set_text(lbl_co2, buf);
}
