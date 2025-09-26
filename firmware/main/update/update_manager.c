#include "update_manager.h"

#include "esp_log.h"

static const char *TAG = "upd_mgr";

void update_manager_init(void)
{
    ESP_LOGI(TAG, "Update manager initialised (stub)");
}

void update_manager_check_sd(void)
{
    ESP_LOGI(TAG, "Checking SD update manifest (stub)");
}
