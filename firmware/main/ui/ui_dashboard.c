#include "ui/ui_dashboard.h"

#include "esp_log.h"
#include "lvgl_port.h"

static const char *TAG = "ui_dashboard";

void ui_dashboard_create(void)
{
    ESP_LOGI(TAG, "Creating dashboard widgets (stub)");
}

void ui_dashboard_refresh(size_t terrarium_count, const terrarium_state_t *first_state)
{
    (void)terrarium_count;
    (void)first_state;
    ESP_LOGD(TAG, "Refreshing dashboard");
    lvgl_port_invalidate();
}
