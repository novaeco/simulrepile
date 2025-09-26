#include "log_manager.h"

#include "esp_log.h"

static const char *TAG = "log_mgr";

void log_manager_init(void)
{
    ESP_LOGI(TAG, "Log manager ready");
}

void log_manager_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    esp_log_writev(ESP_LOG_INFO, TAG, fmt, args);
    va_end(args);
}

void log_manager_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    esp_log_writev(ESP_LOG_ERROR, TAG, fmt, args);
    va_end(args);
}
