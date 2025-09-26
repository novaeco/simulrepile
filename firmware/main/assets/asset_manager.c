#include "asset_manager.h"

#include "app_config.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "asset_mgr";

static asset_item_t s_cache[CONFIG_APP_ASSET_CACHE_SIZE];

void asset_manager_init(void)
{
    memset(s_cache, 0, sizeof(s_cache));
}

static asset_item_t *find_slot(const char *path)
{
    for (size_t i = 0; i < CONFIG_APP_ASSET_CACHE_SIZE; ++i) {
        if (s_cache[i].data && strcmp(s_cache[i].path, path) == 0) {
            return &s_cache[i];
        }
    }
    return NULL;
}

static asset_item_t *allocate_slot(void)
{
    for (size_t i = 0; i < CONFIG_APP_ASSET_CACHE_SIZE; ++i) {
        if (!s_cache[i].data) {
            return &s_cache[i];
        }
    }
    ESP_LOGW(TAG, "Asset cache full, evicting index 0");
    free(s_cache[0].data);
    memset(&s_cache[0], 0, sizeof(s_cache[0]));
    return &s_cache[0];
}

const asset_item_t *asset_manager_get(const char *path)
{
    if (!path) {
        return NULL;
    }
    asset_item_t *slot = find_slot(path);
    if (slot) {
        return slot;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Missing asset %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    asset_item_t *new_slot = allocate_slot();
    if (!new_slot) {
        fclose(f);
        return NULL;
    }

    uint8_t *data = malloc((size_t)size);
    if (!data) {
        fclose(f);
        return NULL;
    }
    fread(data, 1, (size_t)size, f);
    fclose(f);

    strncpy(new_slot->path, path, sizeof(new_slot->path) - 1);
    new_slot->path[sizeof(new_slot->path) - 1] = '\0';
    new_slot->size = (uint32_t)size;
    new_slot->data = data;

    return new_slot;
}

void asset_manager_release(const char *path)
{
    asset_item_t *slot = find_slot(path);
    if (!slot) {
        return;
    }
    free(slot->data);
    memset(slot, 0, sizeof(*slot));
}
