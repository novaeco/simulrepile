#include "sim_engine.h"

#include "app_config.h"
#include "esp_log.h"
#include "sim_presets.h"
#include "virtual_sensors.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

static void update_health(sim_terrarium_state_t *state, float hours)
{
    float temp_target = state->environment.day_temperature_target_c;
    float humidity_target = state->environment.humidity_target_percent;

    float temp_error = state->health.temperature_c - temp_target;
    float humidity_error = state->health.humidity_percent - humidity_target;

    state->health.hydration_level = MAX(0.0f, MIN(1.0f, state->health.hydration_level - humidity_error * 0.0005f * hours));
    state->health.stress_level = MAX(0.0f, MIN(1.0f, state->health.stress_level + fabsf(temp_error) * 0.0003f * hours));
    state->health.shedding_progress = fmodf(state->health.shedding_progress + hours * 0.01f, 1.0f);
    state->health.hunger_level = MAX(0.0f, MIN(1.0f, state->health.hunger_level + hours * 0.02f));
    state->health.activity_level = MAX(0.0f, MIN(1.0f, state->health.activity_level + (0.5f - state->health.stress_level) * 0.01f));
    state->health.hideout_usage = MAX(0.0f, MIN(1.0f, state->health.hideout_usage + state->health.stress_level * 0.01f));
}

void sim_engine_init(void)
{
    memset(s_states, 0, sizeof(s_states));
    s_state_count = 0;
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

static void init_health(sim_health_state_t *health, const sim_species_preset_t *preset)
{
    health->temperature_c = preset->ambient_temp_c;
    health->humidity_percent = preset->humidity_percent;
    health->uv_index = 3.0f;
    health->illumination_lux = preset->humidity_percent > 70.0f ? 100.0f : 300.0f;
    health->hydration_level = 0.8f;
    health->stress_level = 0.2f;
    health->shedding_progress = 0.0f;
    health->hunger_level = 0.3f;
    health->activity_level = 0.5f;
    health->hideout_usage = 0.5f;
}

int sim_engine_add_terrarium(const sim_species_preset_t *preset, const char *nickname)
{
    if (!preset || !nickname || s_state_count >= APP_MAX_TERRARIUMS) {
        return -1;
    }
    sim_terrarium_state_t *state = &s_states[s_state_count];
    memset(state, 0, sizeof(*state));
    snprintf(state->terrarium_id, sizeof(state->terrarium_id), "slot%u", (unsigned)(s_state_count + 1));
    snprintf(state->nickname, sizeof(state->nickname), "%s", nickname);
    state->environment.day_temperature_target_c = preset->basking_temp_c;
    state->environment.night_temperature_target_c = preset->ambient_temp_c;
    state->environment.humidity_target_percent = preset->humidity_percent;
    state->environment.light_day_lux = 5000.0f;
    state->environment.light_night_lux = 5.0f;
    state->environment.day_duration_minutes = 720;
    state->environment.night_duration_minutes = 720;
    state->environment.season_length_days = 90;
    state->species = *preset;
    init_health(&state->health, preset);
    state->care_history_count = 0;
    state->last_feeding_timestamp = 0;
    state->last_mist_timestamp = 0;
    state->last_save_timestamp = 0;

    s_state_count++;

    if (s_event_cb) {
        s_event_cb(state, s_event_ctx);
    }

    ESP_LOGI(TAG, "Terrarium %s added (nickname=%s)", state->terrarium_id, state->nickname);
    return (int)(s_state_count - 1);
}

void sim_engine_register_event_callback(sim_engine_event_cb_t cb, void *user_ctx)
{
    s_event_cb = cb;
    s_event_ctx = user_ctx;
}

void sim_engine_tick(uint32_t elapsed_ms)
{
    float hours = elapsed_ms / 3600000.0f;
    for (size_t i = 0; i < s_state_count; ++i) {
        sim_terrarium_state_t *state = &s_states[i];
        virtual_sensor_inputs_t inputs = virtual_sensors_sample(i, state);
        state->health.temperature_c = inputs.temperature_c;
        state->health.humidity_percent = inputs.humidity_percent;
        state->health.uv_index = inputs.uv_index;
        state->health.illumination_lux = inputs.lux;

        update_health(state, hours);

        if (s_event_cb) {
            s_event_cb(state, s_event_ctx);
        }
    }
}

void sim_engine_set_environment(size_t index, const sim_environment_profile_t *profile)
{
    if (index >= s_state_count || !profile) {
        return;
    }
    s_states[index].environment = *profile;
}

void sim_engine_mark_manual_care(size_t index, const char *description)
{
    if (index >= s_state_count || !description) {
        return;
    }
    sim_terrarium_state_t *state = &s_states[index];
    sim_care_entry_t *entry = &state->care_history[state->care_history_count % (sizeof(state->care_history) / sizeof(state->care_history[0]))];
    snprintf(entry->entry_id, sizeof(entry->entry_id), "evt%u", (unsigned)state->care_history_count);
    snprintf(entry->timestamp_iso8601, sizeof(entry->timestamp_iso8601), "2024-01-01T00:00:00Z");
    snprintf(entry->description, sizeof(entry->description), "%s", description);
    state->care_history_count++;
}
