#include "save_manager.h"

#include "app_config.h"
#include "compression/compression_if.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"
#include "schema_version.h"
#include <cJSON.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "save_mgr";

static void ensure_directories(void)
{
    mkdir(APP_SD_MOUNT_POINT, 0775);
    mkdir(APP_SD_SAVES_DIR, 0775);
}

static void build_path(size_t slot, bool backup, char *out_path, size_t len)
{
    snprintf(out_path, len, "%s/slot%u.%s", APP_SD_SAVES_DIR, (unsigned)(slot + 1), backup ? "bak" : "sav");
}

static cJSON *health_to_json(const sim_health_state_t *health)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "temperature_c", health->temperature_c);
    cJSON_AddNumberToObject(obj, "humidity_percent", health->humidity_percent);
    cJSON_AddNumberToObject(obj, "uv_index", health->uv_index);
    cJSON_AddNumberToObject(obj, "illumination_lux", health->illumination_lux);
    cJSON_AddNumberToObject(obj, "hydration_level", health->hydration_level);
    cJSON_AddNumberToObject(obj, "stress_level", health->stress_level);
    cJSON_AddNumberToObject(obj, "shedding_progress", health->shedding_progress);
    cJSON_AddNumberToObject(obj, "hunger_level", health->hunger_level);
    cJSON_AddNumberToObject(obj, "activity_level", health->activity_level);
    cJSON_AddNumberToObject(obj, "hideout_usage", health->hideout_usage);
    cJSON_AddNumberToObject(obj, "body_condition_score", health->body_condition_score);
    cJSON_AddNumberToObject(obj, "wellness_flags", (double)health->wellness_flags);
    return obj;
}

static cJSON *environment_to_json(const sim_environment_profile_t *env)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "day_temperature_target_c", env->day_temperature_target_c);
    cJSON_AddNumberToObject(obj, "night_temperature_target_c", env->night_temperature_target_c);
    cJSON_AddNumberToObject(obj, "humidity_target_percent", env->humidity_target_percent);
    cJSON_AddNumberToObject(obj, "uv_index_day", env->uv_index_day);
    cJSON_AddNumberToObject(obj, "uv_index_night", env->uv_index_night);
    cJSON_AddNumberToObject(obj, "light_day_lux", env->light_day_lux);
    cJSON_AddNumberToObject(obj, "light_night_lux", env->light_night_lux);
    cJSON_AddNumberToObject(obj, "day_duration_minutes", env->day_duration_minutes);
    cJSON_AddNumberToObject(obj, "night_duration_minutes", env->night_duration_minutes);
    cJSON_AddNumberToObject(obj, "season_length_days", env->season_length_days);
    cJSON_AddNumberToObject(obj, "seasonal_temp_shift_c", env->seasonal_temp_shift_c);
    cJSON_AddNumberToObject(obj, "seasonal_humidity_shift_percent", env->seasonal_humidity_shift_percent);
    return obj;
}

static cJSON *habitat_to_json(const sim_habitat_profile_t *habitat)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "length_cm", habitat->enclosure_length_cm);
    cJSON_AddNumberToObject(obj, "width_cm", habitat->enclosure_width_cm);
    cJSON_AddNumberToObject(obj, "height_cm", habitat->enclosure_height_cm);
    cJSON_AddStringToObject(obj, "substrate", habitat->substrate);
    cJSON_AddBoolToObject(obj, "bioactive", habitat->bioactive);
    return obj;
}

static cJSON *species_to_json(const sim_species_preset_t *species)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "species_id", species->species_id);
    cJSON_AddStringToObject(obj, "display_name", species->display_name);
    cJSON_AddStringToObject(obj, "latin_name", species->latin_name);
    cJSON_AddStringToObject(obj, "cites_appendix", species->cites_appendix);
    cJSON_AddStringToObject(obj, "captive_status", species->captive_status);
    cJSON_AddNumberToObject(obj, "basking_temp_c", species->basking_temp_c);
    cJSON_AddNumberToObject(obj, "ambient_temp_c", species->ambient_temp_c);
    cJSON_AddNumberToObject(obj, "humidity_percent", species->humidity_percent);
    cJSON_AddNumberToObject(obj, "feeding_interval_days", species->feeding_interval_days);
    cJSON_AddNumberToObject(obj, "water_change_interval_days", species->water_change_interval_days);
    cJSON_AddNumberToObject(obj, "supplementation_interval_days", species->supplementation_interval_days);
    cJSON_AddNumberToObject(obj, "uv_index_day", species->uv_index_day);
    cJSON_AddNumberToObject(obj, "uv_index_night", species->uv_index_night);
    return obj;
}

static cJSON *nutrition_to_json(const sim_nutrition_state_t *nutrition)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "weight_grams", nutrition->weight_grams);
    cJSON_AddNumberToObject(obj, "growth_rate_g_per_day", nutrition->growth_rate_g_per_day);
    cJSON_AddNumberToObject(obj, "hydration_ml_per_day", nutrition->hydration_ml_per_day);
    cJSON_AddNumberToObject(obj, "feeding_interval_days", nutrition->feeding_interval_days);
    cJSON_AddNumberToObject(obj, "supplementation_interval_days", nutrition->supplementation_interval_days);
    cJSON_AddNumberToObject(obj, "last_feeding_timestamp", nutrition->last_feeding_timestamp);
    cJSON_AddNumberToObject(obj, "last_supplement_timestamp", nutrition->last_supplement_timestamp);
    cJSON_AddNumberToObject(obj, "last_mist_timestamp", nutrition->last_mist_timestamp);
    return obj;
}

static cJSON *care_history_to_json(const sim_terrarium_state_t *state)
{
    cJSON *array = cJSON_CreateArray();
    const size_t capacity = sizeof(state->care_history) / sizeof(state->care_history[0]);
    size_t entries = MIN(state->care_history_count, capacity);
    size_t start_index = state->care_history_total >= capacity ? (state->care_history_total % capacity) : 0;
    for (size_t i = 0; i < entries; ++i) {
        size_t idx = (start_index + i) % capacity;
        const sim_care_entry_t *entry = &state->care_history[idx];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "entry_id", entry->entry_id);
        cJSON_AddStringToObject(obj, "timestamp", entry->timestamp_iso8601);
        cJSON_AddStringToObject(obj, "description", entry->description);
        cJSON_AddStringToObject(obj, "category", entry->category);
        cJSON_AddItemToArray(array, obj);
    }
    return array;
}

static cJSON *state_to_json(const sim_terrarium_state_t *state)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "magic", "SIMULREPILE");
    cJSON_AddStringToObject(root, "schema_version", SIMULREPILE_SCHEMA_VERSION_STRING);
    cJSON_AddStringToObject(root, "terrarium_id", state->terrarium_id);
    cJSON_AddStringToObject(root, "nickname", state->nickname);
    cJSON_AddNumberToObject(root, "care_history_total", (double)state->care_history_total);
    cJSON_AddNumberToObject(root, "last_save_timestamp", state->last_save_timestamp);
    cJSON_AddNumberToObject(root, "environment_elapsed_minutes", state->environment_elapsed_minutes);
    cJSON_AddBoolToObject(root, "active_day_phase", state->active_day_phase);
    cJSON_AddItemToObject(root, "species", species_to_json(&state->species));
    cJSON_AddItemToObject(root, "environment", environment_to_json(&state->environment));
    cJSON_AddItemToObject(root, "habitat", habitat_to_json(&state->habitat));
    cJSON_AddItemToObject(root, "health", health_to_json(&state->health));
    cJSON_AddItemToObject(root, "nutrition", nutrition_to_json(&state->nutrition));
    cJSON_AddItemToObject(root, "care_history", care_history_to_json(state));
    return root;
}

static bool json_to_health(const cJSON *obj, sim_health_state_t *health)
{
    if (!obj || !health) {
        return false;
    }
    health->temperature_c = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "temperature_c"));
    health->humidity_percent = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "humidity_percent"));
    health->uv_index = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "uv_index"));
    health->illumination_lux = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "illumination_lux"));
    health->hydration_level = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "hydration_level"));
    health->stress_level = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "stress_level"));
    health->shedding_progress = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "shedding_progress"));
    health->hunger_level = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "hunger_level"));
    health->activity_level = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "activity_level"));
    health->hideout_usage = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "hideout_usage"));
    health->body_condition_score = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "body_condition_score"));
    health->wellness_flags = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "wellness_flags"));
    return true;
}

static bool json_to_environment(const cJSON *obj, sim_environment_profile_t *env)
{
    if (!obj || !env) {
        return false;
    }
    env->day_temperature_target_c = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "day_temperature_target_c"));
    env->night_temperature_target_c = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "night_temperature_target_c"));
    env->humidity_target_percent = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "humidity_target_percent"));
    env->uv_index_day = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "uv_index_day"));
    env->uv_index_night = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "uv_index_night"));
    env->light_day_lux = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "light_day_lux"));
    env->light_night_lux = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "light_night_lux"));
    env->day_duration_minutes = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "day_duration_minutes"));
    env->night_duration_minutes = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "night_duration_minutes"));
    env->season_length_days = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "season_length_days"));
    env->seasonal_temp_shift_c = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "seasonal_temp_shift_c"));
    env->seasonal_humidity_shift_percent = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "seasonal_humidity_shift_percent"));
    return true;
}

static bool json_to_habitat(const cJSON *obj, sim_habitat_profile_t *habitat)
{
    if (!obj || !habitat) {
        return false;
    }
    habitat->enclosure_length_cm = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "length_cm"));
    habitat->enclosure_width_cm = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "width_cm"));
    habitat->enclosure_height_cm = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "height_cm"));
    const cJSON *substrate = cJSON_GetObjectItem(obj, "substrate");
    snprintf(habitat->substrate, sizeof(habitat->substrate), "%s", cJSON_IsString(substrate) ? substrate->valuestring : "");
    habitat->bioactive = cJSON_IsTrue(cJSON_GetObjectItem(obj, "bioactive"));
    return true;
}

static bool json_to_species(const cJSON *obj, sim_species_preset_t *species)
{
    if (!obj || !species) {
        return false;
    }
    const cJSON *species_id = cJSON_GetObjectItem(obj, "species_id");
    snprintf(species->species_id, sizeof(species->species_id), "%s", cJSON_IsString(species_id) ? species_id->valuestring : "");
    const cJSON *display_name = cJSON_GetObjectItem(obj, "display_name");
    snprintf(species->display_name, sizeof(species->display_name), "%s", cJSON_IsString(display_name) ? display_name->valuestring : "");
    const cJSON *latin = cJSON_GetObjectItem(obj, "latin_name");
    snprintf(species->latin_name, sizeof(species->latin_name), "%s", cJSON_IsString(latin) ? latin->valuestring : "");
    const cJSON *cites = cJSON_GetObjectItem(obj, "cites_appendix");
    snprintf(species->cites_appendix, sizeof(species->cites_appendix), "%s", cJSON_IsString(cites) ? cites->valuestring : "");
    const cJSON *status = cJSON_GetObjectItem(obj, "captive_status");
    snprintf(species->captive_status, sizeof(species->captive_status), "%s", cJSON_IsString(status) ? status->valuestring : "");
    species->basking_temp_c = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "basking_temp_c"));
    species->ambient_temp_c = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "ambient_temp_c"));
    species->humidity_percent = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "humidity_percent"));
    species->feeding_interval_days = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "feeding_interval_days"));
    species->water_change_interval_days = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "water_change_interval_days"));
    species->supplementation_interval_days = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "supplementation_interval_days"));
    species->uv_index_day = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "uv_index_day"));
    species->uv_index_night = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "uv_index_night"));
    return true;
}

static bool json_to_nutrition(const cJSON *obj, sim_nutrition_state_t *nutrition)
{
    if (!obj || !nutrition) {
        return false;
    }
    nutrition->weight_grams = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "weight_grams"));
    nutrition->growth_rate_g_per_day = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "growth_rate_g_per_day"));
    nutrition->hydration_ml_per_day = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "hydration_ml_per_day"));
    nutrition->feeding_interval_days = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "feeding_interval_days"));
    nutrition->supplementation_interval_days = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "supplementation_interval_days"));
    nutrition->last_feeding_timestamp = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "last_feeding_timestamp"));
    nutrition->last_supplement_timestamp = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "last_supplement_timestamp"));
    nutrition->last_mist_timestamp = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "last_mist_timestamp"));
    return true;
}

static uint32_t json_to_care_history(const cJSON *array, sim_terrarium_state_t *state)
{
    if (!array || !state) {
        return 0;
    }
    const size_t capacity = sizeof(state->care_history) / sizeof(state->care_history[0]);
    size_t index = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, array)
    {
        if (index >= capacity) {
            break;
        }
        sim_care_entry_t *entry = &state->care_history[index];
        const cJSON *entry_id = cJSON_GetObjectItem(item, "entry_id");
        const cJSON *timestamp = cJSON_GetObjectItem(item, "timestamp");
        const cJSON *description = cJSON_GetObjectItem(item, "description");
        const cJSON *category = cJSON_GetObjectItem(item, "category");
        snprintf(entry->entry_id, sizeof(entry->entry_id), "%s", cJSON_IsString(entry_id) ? entry_id->valuestring : "");
        snprintf(entry->timestamp_iso8601, sizeof(entry->timestamp_iso8601), "%s",
                 cJSON_IsString(timestamp) ? timestamp->valuestring : "");
        snprintf(entry->description, sizeof(entry->description), "%s",
                 cJSON_IsString(description) ? description->valuestring : "");
        snprintf(entry->category, sizeof(entry->category), "%s",
                 cJSON_IsString(category) ? category->valuestring : "general");
        ++index;
    }
    return (uint32_t)index;
}

static bool json_to_state(const cJSON *root, sim_terrarium_state_t *state)
{
    if (!root || !state) {
        return false;
    }
    const cJSON *magic = cJSON_GetObjectItem(root, "magic");
    if (!cJSON_IsString(magic) || strcmp(magic->valuestring, "SIMULREPILE") != 0) {
        ESP_LOGE(TAG, "Invalid save magic");
        return false;
    }
    const cJSON *version = cJSON_GetObjectItem(root, "schema_version");
    if (!cJSON_IsString(version)) {
        ESP_LOGE(TAG, "Missing schema version");
        return false;
    }
    snprintf(state->terrarium_id, sizeof(state->terrarium_id), "%s",
             cJSON_GetStringValue(cJSON_GetObjectItem(root, "terrarium_id")) ?: "");
    snprintf(state->nickname, sizeof(state->nickname), "%s",
             cJSON_GetStringValue(cJSON_GetObjectItem(root, "nickname")) ?: "");
    state->care_history_total = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(root, "care_history_total"));
    state->last_save_timestamp = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(root, "last_save_timestamp"));
    state->environment_elapsed_minutes = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(root, "environment_elapsed_minutes"));
    state->active_day_phase = cJSON_IsTrue(cJSON_GetObjectItem(root, "active_day_phase"));

    json_to_species(cJSON_GetObjectItem(root, "species"), &state->species);
    json_to_environment(cJSON_GetObjectItem(root, "environment"), &state->environment);
    json_to_habitat(cJSON_GetObjectItem(root, "habitat"), &state->habitat);
    json_to_health(cJSON_GetObjectItem(root, "health"), &state->health);
    json_to_nutrition(cJSON_GetObjectItem(root, "nutrition"), &state->nutrition);
    uint32_t history_loaded = json_to_care_history(cJSON_GetObjectItem(root, "care_history"), state);
    state->care_history_count = (uint8_t)history_loaded;
    if (state->care_history_total < history_loaded) {
        state->care_history_total = history_loaded;
    }
    return true;
}

static int write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s (%d)", path, errno);
        return -1;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return written == len ? 0 : -1;
}

void save_manager_init(void)
{
    ensure_directories();
}

static int write_payload_with_header(const char *path, const uint8_t *data, size_t len, bool compression_enabled)
{
    save_header_t header = {
        .compression_enabled = compression_enabled ? 1 : 0,
        .reserved = {0},
        .crc32 = esp_rom_crc32_le(0, data, len),
    };

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Unable to open %s", path);
        return -1;
    }
    if (fwrite(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    if (fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int save_manager_save_slot(size_t slot, const sim_terrarium_state_t *state)
{
    if (slot >= SAVE_SLOT_COUNT || !state) {
        return -1;
    }
    ensure_directories();

    sim_terrarium_state_t snapshot = *state;
    snapshot.last_save_timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    cJSON *root = state_to_json(&snapshot);
    if (!root) {
        return -1;
    }

    char *json_str = cJSON_PrintBuffered(root, 1024, 0);
    cJSON_Delete(root);
    if (!json_str) {
        return -1;
    }
    size_t json_len = strlen(json_str);

    uint8_t *payload = (uint8_t *)json_str;
    size_t payload_len = json_len;
    uint8_t *compressed = NULL;
    size_t compressed_len = 0;
    bool compression_enabled = false;

    if (CONFIG_APP_ENABLE_COMPRESSION) {
        if (compression_compress(payload, payload_len, &compressed, &compressed_len) == 0 && compressed && compressed_len > 0) {
            compression_enabled = true;
            payload = compressed;
            payload_len = compressed_len;
        }
    }

    char path[APP_SD_PATH_MAX_LEN];
    char backup_path[APP_SD_PATH_MAX_LEN];
    build_path(slot, false, path, sizeof(path));
    build_path(slot, true, backup_path, sizeof(backup_path));

    // Rotate current save into backup
    rename(path, backup_path);

    int ret = write_payload_with_header(path, payload, payload_len, compression_enabled);

    if (compressed) {
        free(compressed);
    }
    free(json_str);

    if (ret == 0) {
        ESP_LOGI(TAG, "Slot %u saved (%zu bytes, compression=%s)", (unsigned)(slot + 1), payload_len,
                 compression_enabled ? "on" : "off");
        return 0;
    }

    ESP_LOGE(TAG, "Failed to save slot %u, restoring backup", (unsigned)(slot + 1));
    rename(backup_path, path);
    return -1;
}

int save_manager_internal_crc_validate(const uint8_t *data, size_t len, uint32_t expected)
{
    uint32_t computed = esp_rom_crc32_le(0, data, len);
    return computed == expected ? 0 : -1;
}

static int load_payload(const char *path, save_header_t *out_header, uint8_t **out_payload, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (fread(out_header, sizeof(save_header_t), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long remaining = ftell(f) - (long)sizeof(save_header_t);
    fseek(f, sizeof(save_header_t), SEEK_SET);
    if (remaining <= 0) {
        fclose(f);
        return -1;
    }
    uint8_t *payload = malloc((size_t)remaining);
    if (!payload) {
        fclose(f);
        return -1;
    }
    if (fread(payload, 1, (size_t)remaining, f) != (size_t)remaining) {
        free(payload);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out_payload = payload;
    *out_len = (size_t)remaining;
    return 0;
}

static int parse_state_buffer(const uint8_t *buffer, size_t len, sim_terrarium_state_t *out_state)
{
    char *json = malloc(len + 1);
    if (!json) {
        return -1;
    }
    memcpy(json, buffer, len);
    json[len] = '\0';
    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error for save");
        return -1;
    }
    bool ok = json_to_state(root, out_state);
    cJSON_Delete(root);
    return ok ? 0 : -1;
}

static int load_slot_internal(const char *path, sim_terrarium_state_t *out_state)
{
    save_header_t header;
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    if (load_payload(path, &header, &payload, &payload_len) != 0) {
        return -1;
    }

    if (save_manager_internal_crc_validate(payload, payload_len, header.crc32) != 0) {
        free(payload);
        return -2;
    }

    uint8_t *decompressed = payload;
    size_t decompressed_len = payload_len;
    if (header.compression_enabled) {
        if (compression_decompress(payload, payload_len, &decompressed, &decompressed_len) != 0) {
            free(payload);
            return -3;
        }
        free(payload);
    }

    int ret = parse_state_buffer(decompressed, decompressed_len, out_state);
    if (decompressed != payload) {
        free(decompressed);
    }
    return ret;
}

int save_manager_load_slot(size_t slot, sim_terrarium_state_t *out_state)
{
    if (slot >= SAVE_SLOT_COUNT || !out_state) {
        return -1;
    }
    char path[APP_SD_PATH_MAX_LEN];
    char backup_path[APP_SD_PATH_MAX_LEN];
    build_path(slot, false, path, sizeof(path));
    build_path(slot, true, backup_path, sizeof(backup_path));

    int ret = load_slot_internal(path, out_state);
    if (ret == 0) {
        ESP_LOGI(TAG, "Slot %u loaded", (unsigned)(slot + 1));
        return 0;
    }

    ESP_LOGW(TAG, "Primary slot %u failed with code %d, attempting backup", (unsigned)(slot + 1), ret);
    ret = load_slot_internal(backup_path, out_state);
    if (ret == 0) {
        ESP_LOGW(TAG, "Backup slot %u restored", (unsigned)(slot + 1));
        rename(backup_path, path);
        return 0;
    }
    ESP_LOGE(TAG, "Backup slot %u also failed (%d)", (unsigned)(slot + 1), ret);
    return ret;
}

int save_manager_rollback(size_t slot)
{
    if (slot >= SAVE_SLOT_COUNT) {
        return -1;
    }
    char path[APP_SD_PATH_MAX_LEN];
    char backup_path[APP_SD_PATH_MAX_LEN];
    build_path(slot, false, path, sizeof(path));
    build_path(slot, true, backup_path, sizeof(backup_path));
    if (rename(backup_path, path) != 0) {
        ESP_LOGE(TAG, "Failed to rollback slot %u", (unsigned)(slot + 1));
        return -1;
    }
    ESP_LOGI(TAG, "Rollback applied to slot %u", (unsigned)(slot + 1));
    return 0;
}
