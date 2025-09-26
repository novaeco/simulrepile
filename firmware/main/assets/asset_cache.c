#include "assets/asset_cache.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "asset_cache";

esp_err_t asset_cache_init(void)
{
    ESP_LOGI(TAG, "Initializing asset cache (stub)");
    return ESP_OK;
}

void asset_cache_tick(void)
{
    // Placeholder: would evict least-recently-used assets here.
}

esp_err_t asset_cache_get(const char *path, asset_handle_t *handle)
{
    if (!path || !handle) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGD(TAG, "Loading asset: %s", path);
    memset(handle, 0, sizeof(*handle));
    handle->path = path;
    handle->type = ASSET_TYPE_TEXT;
    return ESP_OK;
}

void asset_cache_release(asset_handle_t *handle)
{
    if (!handle) {
        return;
    }
    ESP_LOGD(TAG, "Releasing asset: %s", handle->path ? handle->path : "<null>");
    memset(handle, 0, sizeof(*handle));
}
