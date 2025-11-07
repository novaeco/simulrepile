#include "state/core_state_manager.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#define CORE_STATE_TERRARIUM_COUNT 4
#define PROFILE_PATH_MAX 256

typedef struct {
    uint8_t id;
    char scientific_name[CORE_LINK_NAME_MAX_LEN + 1];
    char common_name[CORE_LINK_NAME_MAX_LEN + 1];
    float base_temp_day;
    float base_temp_night;
    float base_humidity_day;
    float base_humidity_night;
    float base_lux_day;
    float base_lux_night;
    float current_temp_day;
    float current_temp_night;
    float current_humidity_day;
    float current_humidity_night;
    float current_lux_day;
    float current_lux_night;
    float hydration_pct;
    float stress_pct;
    float health_pct;
    float activity_score;
    float cycle_speed;
    float phase_offset;
    float enrichment_factor;
    uint32_t last_feeding_timestamp;
} core_state_slot_t;

static const char *TAG = "core_state_mgr";

static core_state_slot_t s_slots[CORE_STATE_TERRARIUM_COUNT];
static size_t s_slot_count;
static portMUX_TYPE s_slots_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_profile_base_path[PROFILE_PATH_MAX];

typedef struct {
    const char *scientific_name;
    const char *common_name;
    float base_temp_day;
    float base_temp_night;
    float base_humidity_day;
    float base_humidity_night;
    float base_lux_day;
    float base_lux_night;
    float cycle_speed;
    float phase_offset;
    float enrichment_factor;
} core_state_builtin_profile_t;

static const core_state_builtin_profile_t s_builtin_profiles[] = {
    {
        .scientific_name = "Python regius",
        .common_name = "Python royal",
        .base_temp_day = 31.0f,
        .base_temp_night = 24.0f,
        .base_humidity_day = 60.0f,
        .base_humidity_night = 70.0f,
        .base_lux_day = 400.0f,
        .base_lux_night = 5.0f,
        .cycle_speed = 0.03f,
        .phase_offset = 0.0f,
        .enrichment_factor = 1.0f,
    },
    {
        .scientific_name = "Pogona vitticeps",
        .common_name = "Dragon barbu",
        .base_temp_day = 35.0f,
        .base_temp_night = 22.0f,
        .base_humidity_day = 40.0f,
        .base_humidity_night = 50.0f,
        .base_lux_day = 650.0f,
        .base_lux_night = 10.0f,
        .cycle_speed = 0.045f,
        .phase_offset = 1.1f,
        .enrichment_factor = 1.3f,
    },
    {
        .scientific_name = "Correlophus ciliatus",
        .common_name = "Gecko à crête",
        .base_temp_day = 27.0f,
        .base_temp_night = 21.0f,
        .base_humidity_day = 70.0f,
        .base_humidity_night = 85.0f,
        .base_lux_day = 220.0f,
        .base_lux_night = 3.0f,
        .cycle_speed = 0.038f,
        .phase_offset = 2.4f,
        .enrichment_factor = 0.8f,
    },
    {
        .scientific_name = "Eublepharis macularius",
        .common_name = "Gecko léopard",
        .base_temp_day = 33.0f,
        .base_temp_night = 23.0f,
        .base_humidity_day = 45.0f,
        .base_humidity_night = 55.0f,
        .base_lux_day = 320.0f,
        .base_lux_night = 6.0f,
        .cycle_speed = 0.033f,
        .phase_offset = 3.1f,
        .enrichment_factor = 1.1f,
    },
};

static inline float clampf(float value, float min, float max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static uint32_t current_epoch_seconds(void)
{
    uint64_t now_us = esp_timer_get_time();
    return (uint32_t)(CONFIG_CORE_APP_STATE_BASE_EPOCH + (now_us / 1000000ULL));
}

static void apply_slot_defaults(core_state_slot_t *slot, size_t idx, uint32_t now_epoch)
{
    static const float default_cycle_speed[CORE_STATE_TERRARIUM_COUNT] = {0.03f, 0.045f, 0.038f, 0.033f};
    static const float default_phase_offset[CORE_STATE_TERRARIUM_COUNT] = {0.0f, 1.1f, 2.4f, 3.1f};
    static const float default_enrichment[CORE_STATE_TERRARIUM_COUNT] = {1.0f, 1.3f, 0.8f, 1.1f};

    if (idx >= CORE_STATE_TERRARIUM_COUNT) {
        idx = CORE_STATE_TERRARIUM_COUNT - 1;
    }

    slot->current_temp_day = slot->base_temp_day;
    slot->current_temp_night = slot->base_temp_night;
    slot->current_humidity_day = slot->base_humidity_day;
    slot->current_humidity_night = slot->base_humidity_night;
    slot->current_lux_day = slot->base_lux_day;
    slot->current_lux_night = slot->base_lux_night;

    if (!isfinite(slot->cycle_speed) || slot->cycle_speed <= 0.0f) {
        slot->cycle_speed = default_cycle_speed[idx];
    }
    if (!isfinite(slot->phase_offset)) {
        slot->phase_offset = default_phase_offset[idx];
    }
    if (!isfinite(slot->enrichment_factor) || slot->enrichment_factor <= 0.0f) {
        slot->enrichment_factor = default_enrichment[idx];
    }
    if (!isfinite(slot->hydration_pct)) {
        slot->hydration_pct = 88.0f - (float)idx * 3.0f;
    }
    slot->hydration_pct = clampf(slot->hydration_pct, 0.0f, 100.0f);

    if (!isfinite(slot->stress_pct)) {
        slot->stress_pct = 15.0f + (float)idx * 4.0f;
    }
    slot->stress_pct = clampf(slot->stress_pct, 0.0f, 85.0f);

    if (!isfinite(slot->health_pct)) {
        slot->health_pct = 94.0f - (float)idx * 2.0f;
    }
    slot->health_pct = clampf(slot->health_pct, 0.0f, 100.0f);

    if (!isfinite(slot->activity_score)) {
        slot->activity_score = 0.5f;
    }
    slot->activity_score = clampf(slot->activity_score, 0.0f, 1.0f);
    if (slot->last_feeding_timestamp == 0U) {
        slot->last_feeding_timestamp = now_epoch - (uint32_t)(6 * 3600 * (idx + 1));
    }
}

static bool has_json_extension(const char *name)
{
    if (!name) {
        return false;
    }
    size_t len = strlen(name);
    return (len >= 5 && strcasecmp(&name[len - 5], ".json") == 0);
}

static esp_err_t read_file_to_buffer(const char *path, char **out_buffer, size_t *out_size)
{
    if (!path || !out_buffer || !out_size) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        ESP_LOGW(TAG, "Failed to open profile %s: errno=%d", path, errno);
        return ESP_FAIL;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    long length = ftell(file);
    if (length < 0) {
        fclose(file);
        return ESP_FAIL;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return ESP_FAIL;
    }

    char *buffer = (char *)malloc((size_t)length + 1);
    if (!buffer) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (read != (size_t)length) {
        free(buffer);
        return ESP_FAIL;
    }
    buffer[length] = '\0';

    *out_buffer = buffer;
    *out_size = (size_t)length;
    return ESP_OK;
}

static float json_get_number(const cJSON *object, const char *key, float default_value)
{
    const cJSON *node = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(node)) {
        return (float)node->valuedouble;
    }
    return default_value;
}

static void json_copy_string(const cJSON *object, const char *key, char *dest, size_t dest_size, const char *fallback)
{
    if (!dest || dest_size == 0) {
        return;
    }

    const cJSON *node = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(node) && node->valuestring) {
        strlcpy(dest, node->valuestring, dest_size);
    } else if (fallback) {
        strlcpy(dest, fallback, dest_size);
    } else {
        dest[0] = '\0';
    }
}

static bool parse_profile_from_json(const char *path, core_state_slot_t *slot, size_t idx)
{
    char *buffer = NULL;
    size_t size = 0;
    esp_err_t err = read_file_to_buffer(path, &buffer, &size);
    if (err != ESP_OK) {
        return false;
    }

    cJSON *root = cJSON_ParseWithLength(buffer, size);
    free(buffer);
    if (!root) {
        ESP_LOGW(TAG, "Invalid JSON in %s", path);
        return false;
    }

    memset(slot, 0, sizeof(*slot));
    slot->id = (uint8_t)idx;
    slot->cycle_speed = NAN;
    slot->phase_offset = NAN;
    slot->enrichment_factor = NAN;
    slot->hydration_pct = NAN;
    slot->stress_pct = NAN;
    slot->health_pct = NAN;
    slot->activity_score = NAN;

    json_copy_string(root, "scientific_name", slot->scientific_name, sizeof(slot->scientific_name), "Unknown species");
    json_copy_string(root, "common_name", slot->common_name, sizeof(slot->common_name), "Terrarium");

    const cJSON *environment = cJSON_GetObjectItemCaseSensitive(root, "environment");
    if (cJSON_IsObject(environment)) {
        slot->base_temp_day = json_get_number(environment, "temp_day_c", 0.0f);
        slot->base_temp_night = json_get_number(environment, "temp_night_c", 0.0f);
        slot->base_humidity_day = json_get_number(environment, "humidity_day_pct", 0.0f);
        slot->base_humidity_night = json_get_number(environment, "humidity_night_pct", 0.0f);
        slot->base_lux_day = json_get_number(environment, "lux_day", 0.0f);
        slot->base_lux_night = json_get_number(environment, "lux_night", 0.0f);
    }

    const cJSON *id_node = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsNumber(id_node) && id_node->valuedouble >= 0.0) {
        slot->id = (uint8_t)id_node->valuedouble;
    }

    slot->cycle_speed = json_get_number(root, "cycle_speed", slot->cycle_speed);
    slot->phase_offset = json_get_number(root, "phase_offset", slot->phase_offset);
    slot->enrichment_factor = json_get_number(root, "enrichment_factor", slot->enrichment_factor);
    slot->hydration_pct = json_get_number(root, "hydration_pct", slot->hydration_pct);
    slot->stress_pct = json_get_number(root, "stress_pct", slot->stress_pct);
    slot->health_pct = json_get_number(root, "health_pct", slot->health_pct);
    slot->activity_score = json_get_number(root, "activity_score", slot->activity_score);

    const cJSON *feeding_node = cJSON_GetObjectItemCaseSensitive(root, "last_feeding_timestamp");
    if (cJSON_IsNumber(feeding_node) && feeding_node->valuedouble >= 0.0) {
        slot->last_feeding_timestamp = (uint32_t)feeding_node->valuedouble;
    }

    cJSON_Delete(root);

    if (slot->base_temp_day == 0.0f && slot->base_temp_night == 0.0f) {
        ESP_LOGW(TAG, "Profile %s missing temperature data", path);
    }

    return true;
}

typedef struct {
    char path[PROFILE_PATH_MAX];
} profile_path_t;

static int compare_profile_paths(const void *lhs, const void *rhs)
{
    const profile_path_t *a = (const profile_path_t *)lhs;
    const profile_path_t *b = (const profile_path_t *)rhs;
    return strcasecmp(a->path, b->path);
}

static esp_err_t load_profiles_from_directory(const char *directory, core_state_slot_t *slots, size_t *out_count)
{
    if (!directory || !slots || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }

    DIR *dir = opendir(directory);
    if (!dir) {
        return (errno == ENOENT) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    profile_path_t candidates[CORE_STATE_TERRARIUM_COUNT] = {0};
    size_t candidate_count = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (!has_json_extension(entry->d_name)) {
            continue;
        }
        if (candidate_count >= CORE_STATE_TERRARIUM_COUNT) {
            ESP_LOGW(TAG, "Profile limit reached while scanning %s", directory);
            break;
        }

        int written = snprintf(candidates[candidate_count].path, PROFILE_PATH_MAX, "%s/%s", directory, entry->d_name);
        if (written <= 0 || written >= PROFILE_PATH_MAX) {
            ESP_LOGW(TAG, "Profile path too long: %s/%s", directory, entry->d_name);
            continue;
        }
        ++candidate_count;
    }
    closedir(dir);

    if (candidate_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    qsort(candidates, candidate_count, sizeof(profile_path_t), compare_profile_paths);

    size_t loaded = 0;
    uint32_t now_epoch = current_epoch_seconds();
    for (size_t i = 0; i < candidate_count; ++i) {
        if (loaded >= CORE_STATE_TERRARIUM_COUNT) {
            break;
        }

        if (!parse_profile_from_json(candidates[i].path, &slots[loaded], loaded)) {
            continue;
        }
        apply_slot_defaults(&slots[loaded], loaded, now_epoch);
        ++loaded;
    }

    if (loaded == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    *out_count = loaded;
    return ESP_OK;
}

static void load_builtin_profiles(core_state_slot_t *slots, size_t *out_count)
{
    size_t builtin_count = sizeof(s_builtin_profiles) / sizeof(s_builtin_profiles[0]);
    uint32_t now_epoch = current_epoch_seconds();
    size_t count = 0;
    for (size_t i = 0; i < builtin_count && i < CORE_STATE_TERRARIUM_COUNT; ++i) {
        core_state_slot_t *slot = &slots[i];
        memset(slot, 0, sizeof(*slot));
        slot->id = (uint8_t)i;
        strlcpy(slot->scientific_name, s_builtin_profiles[i].scientific_name, sizeof(slot->scientific_name));
        strlcpy(slot->common_name, s_builtin_profiles[i].common_name, sizeof(slot->common_name));
        slot->base_temp_day = s_builtin_profiles[i].base_temp_day;
        slot->base_temp_night = s_builtin_profiles[i].base_temp_night;
        slot->base_humidity_day = s_builtin_profiles[i].base_humidity_day;
        slot->base_humidity_night = s_builtin_profiles[i].base_humidity_night;
        slot->base_lux_day = s_builtin_profiles[i].base_lux_day;
        slot->base_lux_night = s_builtin_profiles[i].base_lux_night;
        slot->cycle_speed = s_builtin_profiles[i].cycle_speed;
        slot->phase_offset = s_builtin_profiles[i].phase_offset;
        slot->enrichment_factor = s_builtin_profiles[i].enrichment_factor;
        apply_slot_defaults(slot, i, now_epoch);
        ++count;
    }

    *out_count = count;
}

esp_err_t core_state_manager_reload_profiles(const char *base_path)
{
    core_state_slot_t new_slots[CORE_STATE_TERRARIUM_COUNT];
    memset(new_slots, 0, sizeof(new_slots));
    size_t new_count = 0;
    esp_err_t err = ESP_FAIL;
    bool base_path_applied = false;

    char preferred[PROFILE_PATH_MAX] = {0};
    if (base_path && base_path[0] != '\0') {
        strlcpy(preferred, base_path, sizeof(preferred));
    } else if (s_profile_base_path[0] != '\0') {
        strlcpy(preferred, s_profile_base_path, sizeof(preferred));
    } else {
        strlcpy(preferred, CONFIG_CORE_STATE_PROFILE_BASE_PATH, sizeof(preferred));
    }

    if (preferred[0] != '\0') {
        err = load_profiles_from_directory(preferred, new_slots, &new_count);
        if (err == ESP_OK && new_count > 0) {
            base_path_applied = true;
            ESP_LOGI(TAG, "Loaded %zu profile(s) from %s", new_count, preferred);
        }
    }

    if ((!base_path_applied || new_count == 0) && strlen(CONFIG_CORE_STATE_PROFILE_SPIFFS_PATH) > 0) {
        char fallback[PROFILE_PATH_MAX];
        strlcpy(fallback, CONFIG_CORE_STATE_PROFILE_SPIFFS_PATH, sizeof(fallback));
        esp_err_t fallback_err = load_profiles_from_directory(fallback, new_slots, &new_count);
        if (fallback_err == ESP_OK && new_count > 0) {
            base_path_applied = true;
            err = ESP_OK;
            strlcpy(preferred, fallback, sizeof(preferred));
            ESP_LOGI(TAG, "Loaded %zu profile(s) from %s (fallback)", new_count, fallback);
        } else if (err == ESP_OK) {
            err = fallback_err;
        }
    }

    if (!base_path_applied || new_count == 0) {
        load_builtin_profiles(new_slots, &new_count);
        err = (new_count > 0) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
        ESP_LOGW(TAG, "Falling back to built-in profiles (%zu)", new_count);
    }

    portENTER_CRITICAL(&s_slots_lock);
    memset(s_slots, 0, sizeof(s_slots));
    if (new_count > 0) {
        memcpy(s_slots, new_slots, new_count * sizeof(core_state_slot_t));
    }
    s_slot_count = new_count;
    if (base_path_applied && preferred[0] != '\0') {
        strlcpy(s_profile_base_path, preferred, sizeof(s_profile_base_path));
    }
    portEXIT_CRITICAL(&s_slots_lock);

    return err;
}

void core_state_manager_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    s_slot_count = 0;
    strlcpy(s_profile_base_path, CONFIG_CORE_STATE_PROFILE_BASE_PATH, sizeof(s_profile_base_path));

    esp_err_t err = core_state_manager_reload_profiles(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Profile reload completed with status %s", esp_err_to_name(err));
    }

    portENTER_CRITICAL(&s_slots_lock);
    size_t count = s_slot_count;
    portEXIT_CRITICAL(&s_slots_lock);
    ESP_LOGI(TAG, "Core state manager initialized (%zu terrariums)", count);
}

void core_state_manager_update(float delta_seconds)
{
    float time_s = (float)(esp_timer_get_time() / 1000000.0);
    portENTER_CRITICAL(&s_slots_lock);
    for (size_t i = 0; i < s_slot_count; ++i) {
        core_state_slot_t *slot = &s_slots[i];
        float wave = sinf(time_s * slot->cycle_speed + slot->phase_offset);
        float wave_secondary = cosf(time_s * slot->cycle_speed * 0.7f + slot->phase_offset * 1.2f);

        slot->current_temp_day = slot->base_temp_day + wave * 1.8f;
        slot->current_temp_night = slot->base_temp_night + wave * 1.0f;
        slot->current_humidity_day = clampf(slot->base_humidity_day + wave_secondary * 6.0f, 30.0f, 95.0f);
        slot->current_humidity_night = clampf(slot->base_humidity_night + wave_secondary * 4.0f, 40.0f, 98.0f);
        slot->current_lux_day = clampf(slot->base_lux_day + wave * 80.0f, 50.0f, 900.0f);
        slot->current_lux_night = clampf(slot->base_lux_night + (wave_secondary + 1.0f) * 2.0f, 0.0f, 20.0f);

        slot->activity_score = clampf(0.45f + 0.4f * sinf(time_s * slot->cycle_speed * 1.3f + slot->phase_offset), 0.0f, 1.0f);

        float stress_trend = (30.0f - slot->activity_score * 45.0f + slot->enrichment_factor * 5.0f) * 0.015f;
        slot->stress_pct = clampf(slot->stress_pct + stress_trend * delta_seconds, 5.0f, 85.0f);

        float hydration_drain = slot->enrichment_factor * (0.20f + 0.05f * (1.0f - slot->activity_score));
        slot->hydration_pct = clampf(slot->hydration_pct - hydration_drain * delta_seconds, 45.0f, 100.0f);

        if (slot->hydration_pct < 55.0f) {
            slot->hydration_pct = 90.0f;
            slot->last_feeding_timestamp = current_epoch_seconds();
            slot->stress_pct = clampf(slot->stress_pct - 6.0f, 0.0f, 85.0f);
            slot->health_pct = clampf(slot->health_pct + 3.5f, 0.0f, 100.0f);
        }

        float hydration_penalty = (70.0f - slot->hydration_pct) * 0.01f;
        float stress_penalty = slot->stress_pct * 0.006f;
        slot->health_pct = clampf(slot->health_pct - (hydration_penalty + stress_penalty) * delta_seconds, 65.0f, 100.0f);
    }
    portEXIT_CRITICAL(&s_slots_lock);
}

void core_state_manager_apply_touch(const core_link_touch_event_t *event)
{
    if (!event) {
        return;
    }

    portENTER_CRITICAL(&s_slots_lock);
    if (s_slot_count == 0) {
        portEXIT_CRITICAL(&s_slots_lock);
        return;
    }

    const uint16_t width = 1024;
    size_t count = s_slot_count;
    uint16_t zone = (count > 0) ? (width / count) : width;
    size_t idx = zone > 0 ? (event->x / zone) : 0;
    if (idx >= count) {
        idx = count - 1;
    }

    core_state_slot_t *slot = &s_slots[idx];
    if (event->type == CORE_LINK_TOUCH_DOWN) {
        slot->stress_pct = clampf(slot->stress_pct - (float)CONFIG_CORE_APP_TOUCH_RELIEF_DELTA, 0.0f, 80.0f);
        slot->activity_score = clampf(slot->activity_score + 0.1f, 0.0f, 1.0f);
    } else if (event->type == CORE_LINK_TOUCH_MOVE) {
        slot->activity_score = clampf(slot->activity_score + 0.02f, 0.0f, 1.0f);
    }
    portEXIT_CRITICAL(&s_slots_lock);
}

void core_state_manager_build_frame(core_link_state_frame_t *frame)
{
    if (!frame) {
        return;
    }

    core_state_slot_t snapshot[CORE_STATE_TERRARIUM_COUNT];
    size_t count = 0;

    portENTER_CRITICAL(&s_slots_lock);
    if (s_slot_count > 0) {
        memcpy(snapshot, s_slots, s_slot_count * sizeof(core_state_slot_t));
        count = s_slot_count;
    }
    portEXIT_CRITICAL(&s_slots_lock);

    memset(frame, 0, sizeof(*frame));
    frame->epoch_seconds = current_epoch_seconds();
    frame->terrarium_count = (uint8_t)count;

    for (uint8_t i = 0; i < count && i < CORE_LINK_MAX_TERRARIUMS; ++i) {
        core_link_terrarium_snapshot_t *snap = &frame->terrariums[i];
        const core_state_slot_t *slot = &snapshot[i];

        snap->terrarium_id = slot->id;
        strncpy(snap->scientific_name, slot->scientific_name, CORE_LINK_NAME_MAX_LEN);
        snap->scientific_name[CORE_LINK_NAME_MAX_LEN] = '\0';
        strncpy(snap->common_name, slot->common_name, CORE_LINK_NAME_MAX_LEN);
        snap->common_name[CORE_LINK_NAME_MAX_LEN] = '\0';

        snap->temp_day_c = slot->current_temp_day;
        snap->temp_night_c = slot->current_temp_night;
        snap->humidity_day_pct = slot->current_humidity_day;
        snap->humidity_night_pct = slot->current_humidity_night;
        snap->lux_day = slot->current_lux_day;
        snap->lux_night = slot->current_lux_night;
        snap->hydration_pct = slot->hydration_pct;
        snap->stress_pct = slot->stress_pct;
        snap->health_pct = slot->health_pct;
        snap->last_feeding_timestamp = slot->last_feeding_timestamp;
        snap->activity_score = slot->activity_score;
    }
}

size_t core_state_manager_get_terrarium_count(void)
{
    size_t count;
    portENTER_CRITICAL(&s_slots_lock);
    count = s_slot_count;
    portEXIT_CRITICAL(&s_slots_lock);
    return count;
}
