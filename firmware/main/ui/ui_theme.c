#include "ui/ui_theme.h"

#include "esp_log.h"
#include "lvgl_port.h"

enum {
    UI_THEME_FLAG_HIGH_CONTRAST = 1 << 0,
};

static const char *TAG = "ui_theme";
static unsigned s_flags = 0;

void ui_theme_apply_default(void)
{
    s_flags = 0;
    ESP_LOGI(TAG, "Applying default theme");
    lvgl_port_invalidate();
}

void ui_theme_apply_high_contrast(bool enabled)
{
    if (enabled) {
        s_flags |= UI_THEME_FLAG_HIGH_CONTRAST;
    } else {
        s_flags &= ~UI_THEME_FLAG_HIGH_CONTRAST;
    }
    ESP_LOGI(TAG, "High contrast %s", enabled ? "ON" : "OFF");
    lvgl_port_invalidate();
}
