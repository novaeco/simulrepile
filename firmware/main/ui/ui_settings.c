#include "ui/ui_settings.h"

#include "esp_log.h"

static const char *TAG = "ui_settings";

void ui_settings_create(void)
{
    ESP_LOGI(TAG, "Creating settings view (stub)");
}

void ui_settings_toggle_accessibility(bool enabled)
{
    ESP_LOGI(TAG, "Accessibility mode %s", enabled ? "enabled" : "disabled");
}
