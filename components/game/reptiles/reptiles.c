#include "reptiles.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

static const reptile_info_t reptile_defs[] = {
    {"Pogona vitticeps", 40.0f, 30.0f, 5.0f, 1.0f, false, true},
    {"Python regius", 32.0f, 60.0f, 3.0f, 0.5f, true, true}
};

static reptile_info_t *reptiles;
static size_t reptile_count;
static const char *TAG = "reptiles";

bool reptiles_load(void) {
    if (reptiles) {
        return true;
    }
    reptile_count = sizeof(reptile_defs) / sizeof(reptile_defs[0]);
    size_t size = reptile_count * sizeof(reptile_info_t);
    reptiles = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!reptiles) {
        ESP_LOGE(TAG, "PSRAM allocation failed");
        return false;
    }
    memcpy(reptiles, reptile_defs, size);
    return true;
}

const reptile_info_t *reptiles_get(size_t *count) {
    if (count) {
        *count = reptile_count;
    }
    return reptiles;
}

const reptile_info_t *reptiles_find(const char *species) {
    if (!reptiles || !species) {
        return NULL;
    }
    for (size_t i = 0; i < reptile_count; ++i) {
        if (strcmp(reptiles[i].species, species) == 0) {
            return &reptiles[i];
        }
    }
    return NULL;
}

bool reptiles_validate(const reptile_info_t *info) {
    if (!info) {
        return false;
    }
    return info->temperature > 0 && info->humidity > 0 && info->uv_index >= 0 &&
           info->terrarium_min_size > 0;
}
