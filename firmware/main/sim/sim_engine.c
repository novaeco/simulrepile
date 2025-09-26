#include "sim_engine.h"

#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sim_presets.h"
#include "virtual_sensors.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

static const char *TAG = "sim_engine";

static sim_terrarium_state_t s_states[APP_MAX_TERRARIUMS];
static size_t s_state_count = 0;
static sim_engine_event_cb_t s_event_cb = NULL;
static void *s_event_ctx = NULL;

static uint64_t monotonic_seconds(void)
{
    uint64_t micros = (uint64_t)esp_timer_get_time();
    return micros / 1000000ULL;
}

static void iso8601_now(char *buffer, size_t len)
{
    if (!buffer || len == 0) {
        return;
    }
    time_t now = (time_t)monotonic_seconds();
    struct tm tm_snapshot;
    gmtime_r(&now, &tm_snapshot);
    strftime(buffer, len, "%Y-%m-%dT%H:%M:%SZ", &tm_snapshot);
}

static void init_health(sim_health_state_t *health, const sim_species_preset_t *preset)
{
    memset(health, 0, sizeof(*health));
    health->temperature_c = preset->ambient_temp_c;
    health->humidity_percent = preset->humidity_percent;
    health->uv_index = preset->uv_index_day;
    health->illumination_lux = 3500.0f;
    health->hydration_level = 0.8f;
    health->stress_level = 0.2f;
    health->shedding_progress = 0.1f;
    health->hunger_level = 0.4f;
    health->activity_level = 0.5f;
    health->hideout_usage = 0.3f;
    health->body_condition_score = 5.0f;
    health->wellness_flags = 0;
}

static void init_habitat(sim_habitat_profile_t *habitat)
{
    habitat->enclosure_length_cm = 120.0f;
    habitat->enclosure_width_cm = 45.0f;
    habitat->enclosure_height_cm = 60.0f;
    snprintf(habitat->substrate, sizeof(habitat->substrate), "Desert mix");
    habitat->bioactive = false;
}

static void init_environment(sim_environment_profile_t *env, const sim_species_preset_t *preset)
{
    env->day_temperature_target_c = preset->basking_temp_c;
    env->night_temperature_target_c = preset->ambient_temp_c - 3.0f;
    env->humidity_target_percent = preset->humidity_percent;
    env->uv_index_day = preset->uv_index_day;
    env->uv_index_night = preset->uv_index_night;
    env->light_day_lux = 5500.0f;
    env->light_night_lux = 5.0f;
    env->day_duration_minutes = 720;
    env->night_duration_minutes = 720;
    env->season_length_days = 90;
    env->seasonal_temp_shift_c = 2.0f;
    env->seasonal_humidity_shift_percent = 10.0f;
}

static void init_nutrition(sim_nutrition_state_t *nutrition, const sim_species_preset_t *preset)
{
    nutrition->weight_grams = 120.0f;
    nutrition->growth_rate_g_per_day = 0.1f;
    nutrition->hydration_ml_per_day = 12.0f;
    nutrition->feeding_interval_days = preset->feeding_interval_days;
    nutrition->supplementation_interval_days = preset->supplementation_interval_days;
    nutrition->last_feeding_timestamp = (uint32_t)monotonic_seconds();
    nutrition->last_supplement_timestamp = nutrition->last_feeding_timestamp;
    nutrition->last_mist_timestamp = nutrition->last_feeding_timestamp;
}

static void emit_update(sim_terrarium_state_t *state)
{
    if (s_event_cb) {
        s_event_cb(state, s_event_ctx);
    }
}

static void push_care_entry(sim_terrarium_state_t *state, const char *category, const char *description)
{
    if (!state || !description) {
        return;
    }
    const size_t capacity = sizeof(state->care_history) / sizeof(state->care_history[0]);
    size_t index = state->care_history_total % capacity;
    sim_care_entry_t *entry = &state->care_history[index];
    snprintf(entry->entry_id, sizeof(entry->entry_id), "evt%06u", (unsigned)state->care_history_total);
    iso8601_now(entry->timestamp_iso8601, sizeof(entry->timestamp_iso8601));
    snprintf(entry->description, sizeof(entry->description), "%s", description);
    snprintf(entry->category, sizeof(entry->category), "%s", category ? category : "general");
    state->care_history_total++;
    if (state->care_history_count < capacity) {
        state->care_history_count++;
    }
}

static float seasonal_adjustment(const sim_environment_profile_t *env, float elapsed_minutes)
{
    if (!env || env->season_length_days == 0) {
        return 0.0f;
    }
    float minutes_per_season = (float)env->season_length_days * 24.0f * 60.0f;
    float phase = fmodf(elapsed_minutes, minutes_per_season) / minutes_per_season;
    return sinf(phase * 2.0f * (float)M_PI);
}

static void update_environment_clock(sim_terrarium_state_t *state, float elapsed_minutes)
{
    float cycle_minutes = (float)state->environment.day_duration_minutes + (float)state->environment.night_duration_minutes;
    if (cycle_minutes <= 0.0f) {
        cycle_minutes = 1440.0f;
    }
    state->environment_elapsed_minutes += elapsed_minutes;
    float mod = fmodf(state->environment_elapsed_minutes, cycle_minutes);
    bool is_day = mod < state->environment.day_duration_minutes;
    state->active_day_phase = is_day;
}

static void update_nutrition(sim_terrarium_state_t *state, float elapsed_hours)
{
    if (!state) {
        return;
    }
    uint64_t now = monotonic_seconds();
    float days_since_feed = (now - state->nutrition.last_feeding_timestamp) / 86400.0f;
    float hunger_target = MIN(1.0f, days_since_feed / MAX(0.1f, state->nutrition.feeding_interval_days));
    state->health.hunger_level = MIN(1.0f, MAX(0.0f, state->health.hunger_level + (hunger_target - state->health.hunger_level) * 0.25f));

    float hydration_decay = elapsed_hours / 24.0f * 0.1f;
    state->health.hydration_level = MIN(1.0f, MAX(0.0f, state->health.hydration_level - hydration_decay));

    state->nutrition.weight_grams = MAX(60.0f, state->nutrition.weight_grams + state->nutrition.growth_rate_g_per_day * (elapsed_hours / 24.0f));
    state->health.body_condition_score = MIN(9.0f, MAX(1.0f, 5.0f + (state->nutrition.weight_grams - 120.0f) / 40.0f));
}

static void update_health(sim_terrarium_state_t *state, const virtual_sensor_inputs_t *inputs, float elapsed_hours)
{
    if (!state || !inputs) {
        return;
    }
    float temp_target = state->active_day_phase ? state->environment.day_temperature_target_c : state->environment.night_temperature_target_c;
    float humidity_target = state->environment.humidity_target_percent;

    float temp_error = inputs->temperature_c - temp_target;
    float humidity_error = inputs->humidity_percent - humidity_target;

    state->health.temperature_c = inputs->temperature_c;
    state->health.humidity_percent = inputs->humidity_percent;
    state->health.uv_index = inputs->uv_index;
    state->health.illumination_lux = inputs->lux;

    state->health.hydration_level = MIN(1.0f, MAX(0.0f, state->health.hydration_level - humidity_error * 0.0003f * elapsed_hours));
    state->health.stress_level = MIN(1.0f, MAX(0.0f, state->health.stress_level + fabsf(temp_error) * 0.0004f * elapsed_hours));
    state->health.shedding_progress = fmodf(state->health.shedding_progress + elapsed_hours * 0.01f, 1.0f);
    state->health.activity_level = MIN(1.0f, MAX(0.0f, state->health.activity_level + (0.6f - state->health.stress_level) * 0.02f));
    state->health.hideout_usage = MIN(1.0f, MAX(0.0f, state->health.hideout_usage + state->health.stress_level * 0.01f));

    if (state->health.stress_level > 0.8f || state->health.hydration_level < 0.3f) {
        state->health.wellness_flags |= 0x1;
    }
}

void sim_engine_init(void)
{
    memset(s_states, 0, sizeof(s_states));
    s_state_count = 0;
    s_event_cb = NULL;
    s_event_ctx = NULL;
    ESP_LOGI(TAG, "Simulation engine initialised (max=%d)", APP_MAX_TERRARIUMS);
}

size_t sim_engine_terrarium_count(void)
{
    return s_state_count;
}

const sim_terrarium_state_t *sim_engine_get_state(size_t index)
{
    if (index >= s_state_count) {
        return NULL;
    }
    return &s_states[index];
}

static sim_terrarium_state_t *allocate_state(void)
{
    if (s_state_count >= APP_MAX_TERRARIUMS) {
        return NULL;
    }
    return &s_states[s_state_count++];
}

int sim_engine_add_terrarium(const sim_species_preset_t *preset, const char *nickname)
{
    if (!preset || !nickname) {
        return -1;
    }
    sim_terrarium_state_t *state = allocate_state();
    if (!state) {
        ESP_LOGE(TAG, "Unable to allocate terrarium state");
        return -1;
    }
    memset(state, 0, sizeof(*state));
    snprintf(state->terrarium_id, sizeof(state->terrarium_id), "slot%u", (unsigned)s_state_count);
    snprintf(state->nickname, sizeof(state->nickname), "%s", nickname);
    state->species = *preset;
    init_environment(&state->environment, preset);
    init_habitat(&state->habitat);
    init_health(&state->health, preset);
    init_nutrition(&state->nutrition, preset);
    state->environment_elapsed_minutes = 0.0f;
    state->active_day_phase = true;
    emit_update(state);
    ESP_LOGI(TAG, "Terrarium %s added for %s", state->terrarium_id, state->species.display_name);
    return (int)(s_state_count - 1);
}

int sim_engine_attach_state(const sim_terrarium_state_t *snapshot)
{
    if (!snapshot) {
        return -1;
    }
    sim_terrarium_state_t *state = allocate_state();
    if (!state) {
        return -1;
    }
    *state = *snapshot;
    ESP_LOGI(TAG, "Terrarium %s restored from snapshot", state->terrarium_id);
    emit_update(state);
    return (int)(s_state_count - 1);
}

void sim_engine_register_event_callback(sim_engine_event_cb_t cb, void *user_ctx)
{
    s_event_cb = cb;
    s_event_ctx = user_ctx;
}

void sim_engine_tick(uint32_t elapsed_ms)
{
    float elapsed_hours = elapsed_ms / 3600000.0f;
    float elapsed_minutes = elapsed_hours * 60.0f;
    for (size_t i = 0; i < s_state_count; ++i) {
        sim_terrarium_state_t *state = &s_states[i];
        update_environment_clock(state, elapsed_minutes);

        float seasonal = seasonal_adjustment(&state->environment, state->environment_elapsed_minutes);
        sim_environment_profile_t adjusted_env = state->environment;
        adjusted_env.day_temperature_target_c += state->environment.seasonal_temp_shift_c * seasonal;
        adjusted_env.night_temperature_target_c += state->environment.seasonal_temp_shift_c * seasonal;
        adjusted_env.humidity_target_percent += state->environment.seasonal_humidity_shift_percent * seasonal;

        virtual_sensor_inputs_t inputs = virtual_sensors_sample(i, state);
        inputs.temperature_c += (adjusted_env.day_temperature_target_c - state->environment.day_temperature_target_c) * 0.1f;
        inputs.humidity_percent += state->environment.seasonal_humidity_shift_percent * seasonal * 0.05f;

        update_health(state, &inputs, elapsed_hours);
        update_nutrition(state, elapsed_hours);

        emit_update(state);
    }
}

void sim_engine_set_environment(size_t index, const sim_environment_profile_t *profile)
{
    if (index >= s_state_count || !profile) {
        return;
    }
    s_states[index].environment = *profile;
    emit_update(&s_states[index]);
}

static void register_care_event(sim_terrarium_state_t *state, const char *category, const char *description)
{
    push_care_entry(state, category, description);
    emit_update(state);
}

void sim_engine_feed(size_t index, const char *notes)
{
    if (index >= s_state_count) {
        return;
    }
    sim_terrarium_state_t *state = &s_states[index];
    state->nutrition.last_feeding_timestamp = (uint32_t)monotonic_seconds();
    state->health.hunger_level = MAX(0.0f, state->health.hunger_level - 0.5f);
    register_care_event(state, "feeding", notes ? notes : "Repas distribué");
}

void sim_engine_mist(size_t index, const char *notes)
{
    if (index >= s_state_count) {
        return;
    }
    sim_terrarium_state_t *state = &s_states[index];
    state->nutrition.last_mist_timestamp = (uint32_t)monotonic_seconds();
    state->health.hydration_level = MIN(1.0f, state->health.hydration_level + 0.3f);
    register_care_event(state, "hydration", notes ? notes : "Brumisation effectuée");
}

void sim_engine_mark_manual_care(size_t index, const char *description)
{
    if (index >= s_state_count || !description) {
        return;
    }
    register_care_event(&s_states[index], "manual", description);
}
