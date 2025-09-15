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
        cJSON *needs = cJSON_GetObjectItem(item, "needs");
        cJSON *legal = cJSON_GetObjectItem(item, "legal");
        if (!cJSON_IsString(species) || !cJSON_IsObject(needs) || !cJSON_IsObject(legal)) {
            ESP_LOGW(TAG, "Incomplete data for reptile index %d", (int)i);
            continue;
        }

        cJSON *temperature = cJSON_GetObjectItem(needs, "temperature");
        cJSON *humidity = cJSON_GetObjectItem(needs, "humidity");
        cJSON *uv_index = cJSON_GetObjectItem(needs, "uv_index");
        cJSON *terrarium_min_size = cJSON_GetObjectItem(needs, "terrarium_min_size");
        cJSON *growth_rate = cJSON_GetObjectItem(needs, "growth_rate");
        cJSON *health_max = cJSON_GetObjectItem(needs, "health_max");

        cJSON *requires_authorisation = cJSON_GetObjectItem(legal, "requires_authorisation");
        cJSON *requires_certificat = cJSON_GetObjectItem(legal, "requires_certificat");
        cJSON *fr_allowed = cJSON_GetObjectItem(legal, "fr_allowed");
        cJSON *eu_allowed = cJSON_GetObjectItem(legal, "eu_allowed");
        cJSON *intl_allowed = cJSON_GetObjectItem(legal, "intl_allowed");

        if (!cJSON_IsNumber(temperature) || !cJSON_IsNumber(humidity) ||
            !cJSON_IsNumber(uv_index) || !cJSON_IsNumber(terrarium_min_size) ||
            !cJSON_IsNumber(growth_rate) || !cJSON_IsNumber(health_max) ||
            !cJSON_IsBool(requires_authorisation) ||
            !cJSON_IsBool(requires_certificat) ||
            !cJSON_IsBool(fr_allowed) || !cJSON_IsBool(eu_allowed) ||
            !cJSON_IsBool(intl_allowed)) {
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
        reptiles[i].needs.temperature = (float)temperature->valuedouble;
        reptiles[i].needs.humidity = (float)humidity->valuedouble;
        reptiles[i].needs.uv_index = (float)uv_index->valuedouble;
        reptiles[i].needs.terrarium_min_size = (float)terrarium_min_size->valuedouble;
        reptiles[i].needs.growth_rate = (float)growth_rate->valuedouble;
        reptiles[i].needs.max_health = (float)health_max->valuedouble;
        reptiles[i].legal.requires_authorisation = cJSON_IsTrue(requires_authorisation);
        reptiles[i].legal.requires_certificat = cJSON_IsTrue(requires_certificat);
        reptiles[i].legal.allowed_fr = cJSON_IsTrue(fr_allowed);
        reptiles[i].legal.allowed_eu = cJSON_IsTrue(eu_allowed);
        reptiles[i].legal.allowed_international = cJSON_IsTrue(intl_allowed);
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

bool reptiles_add(const reptile_info_t *info) {
    if (!info || !reptiles_validate(info)) {
        return false;
    }

    reptile_info_t *new_array = heap_caps_realloc(reptiles, (reptile_count + 1) * sizeof(reptile_info_t),
                                                  MALLOC_CAP_SPIRAM);
    if (!new_array) {
        ESP_LOGE(TAG, "PSRAM reallocation failed");
        return false;
    }
    reptiles = new_array;

    reptile_info_t *dest = &reptiles[reptile_count];
    memset(dest, 0, sizeof(*dest));

    char *sp = heap_caps_malloc(strlen(info->species) + 1, MALLOC_CAP_SPIRAM);
    if (!sp) {
        ESP_LOGE(TAG, "PSRAM allocation failed for species");
        return false;
    }
    strcpy(sp, info->species);

    dest->species = sp;
    dest->needs = info->needs;
    dest->legal = info->legal;
    reptile_count++;
    return true;
}

bool reptiles_validate(const reptile_info_t *info) {
    if (!info) {
        return false;
    }
    bool bio_ok = info->needs.temperature > 0 && info->needs.humidity > 0 &&
                  info->needs.uv_index >= 0 && info->needs.terrarium_min_size > 0 &&
                  info->needs.growth_rate > 0 && info->needs.max_health > 0;
    if (!bio_ok) {
        return false;
    }
    bool legal_ok = info->legal.allowed_fr && info->legal.allowed_eu &&
                    info->legal.allowed_international &&
                    !info->legal.requires_authorisation &&
                    !info->legal.requires_certificat;
    if (!legal_ok) {
        ESP_LOGW(TAG, "Legal requirements not satisfied for %s", info->species);
        return false;
    }
    return true;
}
