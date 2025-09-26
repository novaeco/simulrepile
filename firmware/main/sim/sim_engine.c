#include "sim/sim_engine.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "sim/presets.h"

#define MAX_TERRARIUMS 4

static const char *TAG = "sim_engine";
static terrarium_state_t s_terrariums[MAX_TERRARIUMS];
static size_t s_terrarium_count = 0;
static float s_time_accumulator = 0.0f;

void sim_engine_init(void)
{
    memset(s_terrariums, 0, sizeof(s_terrariums));
    size_t preset_count = 0;
    const reptile_profile_t *presets = sim_presets_get_default(&preset_count);
    s_terrarium_count = preset_count < MAX_TERRARIUMS ? preset_count : MAX_TERRARIUMS;
    for (size_t i = 0; i < s_terrarium_count; ++i) {
        s_terrariums[i].profile = &presets[i];
        s_terrariums[i].current_environment = presets[i].environment;
        s_terrariums[i].health.hydration_pct = 85.0f;
        s_terrariums[i].health.health_pct = 95.0f;
        s_terrariums[i].health.stress_pct = 10.0f;
        s_terrariums[i].activity_score = 0.5f;
    }
    ESP_LOGI(TAG, "Simulation initialized with %d terrariums", (int)s_terrarium_count);
}

void sim_engine_step(float delta_seconds)
{
    s_time_accumulator += delta_seconds;
    for (size_t i = 0; i < s_terrarium_count; ++i) {
        terrarium_state_t *state = &s_terrariums[i];
        float phase = sinf(s_time_accumulator * 0.01f + i);
        state->current_environment.temp_day_c = state->profile->environment.temp_day_c + phase;
        state->current_environment.humidity_day_pct = state->profile->environment.humidity_day_pct + phase * 5.0f;
        state->activity_score = 0.5f + 0.5f * phase;
        state->health.stress_pct = 15.0f + 10.0f * (1.0f - state->activity_score);
        state->health.health_pct = 90.0f + 5.0f * state->activity_score;
    }
}

size_t sim_engine_get_count(void)
{
    return s_terrarium_count;
}

const terrarium_state_t *sim_engine_get_state(size_t index)
{
    if (index >= s_terrarium_count) {
        return NULL;
    }
    return &s_terrariums[index];
}
