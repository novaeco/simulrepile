#include "ui/ui_root.h"

#include "esp_log.h"
#include "lvgl_port.h"
#include "sim/sim_engine.h"
#include "ui/ui_dashboard.h"
#include "ui/ui_docs.h"
#include "ui/ui_settings.h"
#include "ui/ui_slots.h"
#include "ui/ui_theme.h"

static const char *TAG = "ui_root";

esp_err_t ui_root_init(void)
{
    ESP_LOGI(TAG, "Initializing UI root");
    lvgl_port_lock();
    ui_theme_apply_default();
    ui_slots_create();
    ui_dashboard_create();
    ui_docs_create();
    ui_settings_create();
    lvgl_port_unlock();
    return ESP_OK;
}

void ui_root_show_boot_splash(void)
{
    ESP_LOGI(TAG, "Displaying splash screen (stub)");
}

void ui_root_show_disclaimer(void)
{
    ESP_LOGI(TAG, "Displaying disclaimer overlay (stub)");
}

void ui_root_update(void)
{
    lvgl_port_lock();
    ui_dashboard_refresh(sim_engine_get_count(), sim_engine_get_state(0));
    ui_slots_refresh();
    lvgl_port_unlock();
}
