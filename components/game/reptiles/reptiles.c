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

        cJSON *cites = cJSON_GetObjectItem(legal, "cites");
        cJSON *requires_authorisation = cJSON_GetObjectItem(legal, "requires_authorisation");
        cJSON *requires_cdc = cJSON_GetObjectItem(legal, "requires_cdc");
        cJSON *requires_certificat = cJSON_GetObjectItem(legal, "requires_certificat");
        cJSON *requires_declaration = cJSON_GetObjectItem(legal, "requires_declaration");
        cJSON *requires_marking = cJSON_GetObjectItem(legal, "requires_marking");
        cJSON *dangerous = cJSON_GetObjectItem(legal, "dangerous");
        cJSON *max_without_permit = cJSON_GetObjectItem(legal, "max_without_permit");
        cJSON *max_total = cJSON_GetObjectItem(legal, "max_total");
        cJSON *fr_allowed = cJSON_GetObjectItem(legal, "fr_allowed");
        cJSON *eu_allowed = cJSON_GetObjectItem(legal, "eu_allowed");
        cJSON *intl_allowed = cJSON_GetObjectItem(legal, "intl_allowed");

        if (!cJSON_IsNumber(temperature) || !cJSON_IsNumber(humidity) ||
            !cJSON_IsNumber(uv_index) || !cJSON_IsNumber(terrarium_min_size) ||
            !cJSON_IsNumber(growth_rate) || !cJSON_IsNumber(health_max) ||
            !cJSON_IsString(cites) || !cJSON_IsBool(requires_authorisation) ||
            !cJSON_IsBool(requires_cdc) || !cJSON_IsBool(requires_certificat) ||
            !cJSON_IsBool(requires_declaration) || !cJSON_IsBool(requires_marking) ||
            !cJSON_IsBool(dangerous) || !cJSON_IsNumber(max_without_permit) ||
            !cJSON_IsNumber(max_total) || !cJSON_IsBool(fr_allowed) || !cJSON_IsBool(eu_allowed) ||
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
        if (strcmp(cites->valuestring, "I") == 0) {
            reptiles[i].legal.cites = REPTILE_CITES_I;
        } else if (strcmp(cites->valuestring, "II") == 0) {
            reptiles[i].legal.cites = REPTILE_CITES_II;
        } else if (strcmp(cites->valuestring, "III") == 0) {
            reptiles[i].legal.cites = REPTILE_CITES_III;
        } else {
            reptiles[i].legal.cites = REPTILE_CITES_NONE;
        }
        reptiles[i].legal.requires_authorisation = cJSON_IsTrue(requires_authorisation);
        reptiles[i].legal.requires_cdc = cJSON_IsTrue(requires_cdc);
        reptiles[i].legal.requires_certificat = cJSON_IsTrue(requires_certificat);
        reptiles[i].legal.requires_declaration = cJSON_IsTrue(requires_declaration);
        reptiles[i].legal.requires_marking = cJSON_IsTrue(requires_marking);
        reptiles[i].legal.dangerous = cJSON_IsTrue(dangerous);
        double max_without_value = max_without_permit->valuedouble;
        if (max_without_value < 0.0) {
            max_without_value = 0.0;
        }
        if (max_without_value > UINT16_MAX) {
            max_without_value = UINT16_MAX;
        }
        reptiles[i].legal.max_without_permit = (uint16_t)(max_without_value + 0.5);
        double max_total_value = max_total->valuedouble;
        if (max_total_value < 0.0) {
            max_total_value = 0.0;
        }
        if (max_total_value > UINT16_MAX) {
            max_total_value = UINT16_MAX;
        }
        reptiles[i].legal.max_total = (uint16_t)(max_total_value + 0.5);
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

bool reptiles_add(const reptile_info_t *info, const reptile_user_ctx_t *ctx) {
    if (!info || !reptiles_validate(info, ctx)) {
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

bool reptiles_check_compliance(const reptile_legal_t *legal,
                               const reptile_user_ctx_t *ctx) {
    if (!legal || !ctx) {
        return false;
    }
    bool region_ok = (ctx->region == REPTILE_REGION_FR && legal->allowed_fr) ||
                     (ctx->region == REPTILE_REGION_EU && legal->allowed_eu) ||
                     (ctx->region == REPTILE_REGION_INTL && legal->allowed_international);
    if (!region_ok) {
        ESP_LOGW(TAG, "Region not authorised for species");
        return false;
    }

    if (legal->cites > ctx->cites_permit) {
        ESP_LOGW(TAG, "Insufficient CITES permit (need %d have %d)",
                 (int)legal->cites, (int)ctx->cites_permit);
        return false;
    }
    if (legal->requires_authorisation && !ctx->has_authorisation) {
        ESP_LOGW(TAG, "Missing prefectoral authorisation");
        return false;
    }
    if (legal->requires_cdc && !ctx->has_cdc) {
        ESP_LOGW(TAG, "Missing certificat de capacit\xC3\xA9");
        return false;
    }
    if (legal->requires_certificat && !ctx->has_certificat) {
        ESP_LOGW(TAG, "Missing additional certificate");
        return false;
    }
    if (legal->requires_declaration && !ctx->has_declaration) {
        ESP_LOGW(TAG, "Missing mandatory declaration");
        return false;
    }
    if (legal->requires_marking && !ctx->has_marking_system) {
        ESP_LOGW(TAG, "Missing identification/marking capability");
        return false;
    }
    if (legal->dangerous && !ctx->has_dangerous_permit) {
        ESP_LOGW(TAG, "Dangerous species permit required");
        return false;
    }

    uint16_t declared = ctx->declared_specimens;
    if (declared > 0U) {
        if (!ctx->has_cdc && legal->max_without_permit > 0U &&
            declared > legal->max_without_permit) {
            ESP_LOGW(TAG,
                     "Declared specimens %u exceed limit %u without CDC/APD",
                     (unsigned)declared, (unsigned)legal->max_without_permit);
            return false;
        }
        if (legal->max_total > 0U && declared > legal->max_total) {
            ESP_LOGW(TAG,
                     "Declared specimens %u exceed regulatory cap %u",
                     (unsigned)declared, (unsigned)legal->max_total);
            return false;
        }
    }
    return true;
}

bool reptiles_validate(const reptile_info_t *info,
                       const reptile_user_ctx_t *ctx) {
    if (!info || !ctx) {
        return false;
    }
    bool bio_ok = info->needs.temperature > 0 && info->needs.humidity > 0 &&
                  info->needs.uv_index >= 0 && info->needs.terrarium_min_size > 0 &&
                  info->needs.growth_rate > 0 && info->needs.max_health > 0;
    if (!bio_ok) {
        return false;
    }
    if (!reptiles_check_compliance(&info->legal, ctx)) {
        ESP_LOGW(TAG, "Legal requirements not satisfied for %s", info->species);
        return false;
    }
    return true;
}
