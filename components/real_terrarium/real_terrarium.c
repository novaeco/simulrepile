#include "real_terrarium.h"
#include "esp_log.h"

static const char *TAG = "real_terrarium";

void real_terrarium_init(void)
{
    ESP_LOGI(TAG, "Initializing real terrarium module");
}

void real_terrarium_show_main_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Gestion Terrariums Réels");
    lv_obj_center(label);
    lv_scr_load(scr);
}
