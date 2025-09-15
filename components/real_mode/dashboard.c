#include "dashboard.h"
#include "esp_log.h"

static const char *TAG = "dashboard";

static lv_obj_t *scr;
static lv_obj_t *lbl_temp;
static lv_obj_t *lbl_hum;
static lv_obj_t *lbl_lux;
static lv_obj_t *lbl_co2;

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
