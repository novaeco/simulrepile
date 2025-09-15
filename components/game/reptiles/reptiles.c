#include "reptiles.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdint.h>

extern const uint8_t reptiles_reptiles_json_start[] asm("_binary_reptiles_reptiles_json_start");
extern const uint8_t reptiles_reptiles_json_end[]   asm("_binary_reptiles_reptiles_json_end");

static reptile_info_t *reptiles;
static size_t reptile_count;
static const char *TAG = "reptiles";

bool reptiles_load(void) {
    if (reptiles) {
        return true;
    }

    size_t json_size = reptiles_reptiles_json_end - reptiles_reptiles_json_start;
    char *json_data = heap_caps_malloc(json_size + 1, MALLOC_CAP_SPIRAM);
    if (!json_data) {
        ESP_LOGE(TAG, "PSRAM allocation failed for JSON");
        return false;
    }
    memcpy(json_data, reptiles_reptiles_json_start, json_size);
    json_data[json_size] = '\0';

    cJSON *root = cJSON_Parse(json_data);
    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "Invalid reptiles JSON");
        if (root) {
            cJSON_Delete(root);
        }
        heap_caps_free(json_data);
        return false;
    }

    reptile_count = cJSON_GetArraySize(root);
    reptiles = heap_caps_calloc(reptile_count, sizeof(reptile_info_t), MALLOC_CAP_SPIRAM);
    if (!reptiles) {
        ESP_LOGE(TAG, "PSRAM allocation failed");
        cJSON_Delete(root);
        heap_caps_free(json_data);
        return false;
    }

    for (size_t i = 0; i < reptile_count; ++i) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        cJSON *species = cJSON_GetObjectItem(item, "species");
        cJSON *temperature = cJSON_GetObjectItem(item, "temperature");
        cJSON *humidity = cJSON_GetObjectItem(item, "humidity");
        cJSON *uv_index = cJSON_GetObjectItem(item, "uv_index");
        cJSON *terrarium_min_size = cJSON_GetObjectItem(item, "terrarium_min_size");
        cJSON *requires_authorisation = cJSON_GetObjectItem(item, "requires_authorisation");
        cJSON *requires_certificat = cJSON_GetObjectItem(item, "requires_certificat");

        if (!cJSON_IsString(species) || !cJSON_IsNumber(temperature) ||
            !cJSON_IsNumber(humidity) || !cJSON_IsNumber(uv_index) ||
            !cJSON_IsNumber(terrarium_min_size) ||
            !cJSON_IsBool(requires_authorisation) ||
            !cJSON_IsBool(requires_certificat)) {
            ESP_LOGW(TAG, "Incomplete data for reptile index %d", (int)i);
            continue;
        }

        char *sp = heap_caps_malloc(strlen(species->valuestring) + 1, MALLOC_CAP_SPIRAM);
        if (!sp) {
            ESP_LOGE(TAG, "PSRAM allocation failed for species");
            continue;
        }
        strcpy(sp, species->valuestring);

        reptiles[i].species = sp;
        reptiles[i].temperature = (float)temperature->valuedouble;
        reptiles[i].humidity = (float)humidity->valuedouble;
        reptiles[i].uv_index = (float)uv_index->valuedouble;
        reptiles[i].terrarium_min_size = (float)terrarium_min_size->valuedouble;
        reptiles[i].requires_authorisation = cJSON_IsTrue(requires_authorisation);
        reptiles[i].requires_certificat = cJSON_IsTrue(requires_certificat);
    }

    cJSON_Delete(root);
    heap_caps_free(json_data);
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
    bool bio_ok = info->temperature > 0 && info->humidity > 0 &&
                  info->uv_index >= 0 && info->terrarium_min_size > 0;
    if (!bio_ok) {
        return false;
    }
    if (info->requires_authorisation || info->requires_certificat) {
        ESP_LOGW(TAG, "Legal requirements not satisfied for %s", info->species);
        return false;
    }
    return true;
}
