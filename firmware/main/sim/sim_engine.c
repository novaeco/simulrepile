#include "sim/sim_engine.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "link/core_link_protocol.h"
#include "esp_err.h"
#include "sim/presets.h"

#define MAX_TERRARIUMS 4

static const char *TAG = "sim_engine";
static terrarium_state_t s_terrariums[MAX_TERRARIUMS];
static size_t s_terrarium_count = 0;
static float s_time_accumulator = 0.0f;
static bool s_remote_active = false;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static reptile_profile_t s_remote_profiles[MAX_TERRARIUMS];
static char s_remote_scientific_names[MAX_TERRARIUMS][CORE_LINK_NAME_MAX_LEN + 1];
static char s_remote_common_names[MAX_TERRARIUMS][CORE_LINK_NAME_MAX_LEN + 1];

void sim_engine_init(void)
{
    portENTER_CRITICAL(&s_state_lock);
    memset(s_terrariums, 0, sizeof(s_terrariums));
    memset(s_remote_profiles, 0, sizeof(s_remote_profiles));
    memset(s_remote_scientific_names, 0, sizeof(s_remote_scientific_names));
    memset(s_remote_common_names, 0, sizeof(s_remote_common_names));
    s_remote_active = false;

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
    portEXIT_CRITICAL(&s_state_lock);
    ESP_LOGI(TAG, "Simulation initialized with %d terrariums", (int)s_terrarium_count);
}

void sim_engine_step(float delta_seconds)
{
    if (s_remote_active) {
        (void)delta_seconds;
        return;
    }
    portENTER_CRITICAL(&s_state_lock);
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
    portEXIT_CRITICAL(&s_state_lock);
}

size_t sim_engine_get_count(void)
{
    size_t count = 0;
    portENTER_CRITICAL(&s_state_lock);
    count = s_terrarium_count;
    portEXIT_CRITICAL(&s_state_lock);
    return count;
}

const terrarium_state_t *sim_engine_get_state(size_t index)
{
    const terrarium_state_t *state = NULL;
    portENTER_CRITICAL(&s_state_lock);
    if (index < s_terrarium_count) {
        state = &s_terrariums[index];
    }
    portEXIT_CRITICAL(&s_state_lock);
    return state;
}

esp_err_t sim_engine_apply_remote_snapshot(const core_link_state_frame_t *frame)
{
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t count = frame->terrarium_count;
    if (count > MAX_TERRARIUMS) {
        count = MAX_TERRARIUMS;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_terrarium_count = count;
    for (size_t i = 0; i < count; ++i) {
        const core_link_terrarium_snapshot_t *snap = &frame->terrariums[i];
        strncpy(s_remote_scientific_names[i], snap->scientific_name, CORE_LINK_NAME_MAX_LEN);
        s_remote_scientific_names[i][CORE_LINK_NAME_MAX_LEN] = '\0';
        strncpy(s_remote_common_names[i], snap->common_name, CORE_LINK_NAME_MAX_LEN);
        s_remote_common_names[i][CORE_LINK_NAME_MAX_LEN] = '\0';

        s_remote_profiles[i].scientific_name = s_remote_scientific_names[i];
        s_remote_profiles[i].common_name = s_remote_common_names[i];
        s_remote_profiles[i].environment.temp_day_c = snap->temp_day_c;
        s_remote_profiles[i].environment.temp_night_c = snap->temp_night_c;
        s_remote_profiles[i].environment.humidity_day_pct = snap->humidity_day_pct;
        s_remote_profiles[i].environment.humidity_night_pct = snap->humidity_night_pct;
        s_remote_profiles[i].environment.lux_day = snap->lux_day;
        s_remote_profiles[i].environment.lux_night = snap->lux_night;
        s_remote_profiles[i].feeding_interval_days = 0;

        terrarium_state_t *state = &s_terrariums[i];
        state->profile = &s_remote_profiles[i];
        state->current_environment.temp_day_c = snap->temp_day_c;
        state->current_environment.temp_night_c = snap->temp_night_c;
        state->current_environment.humidity_day_pct = snap->humidity_day_pct;
        state->current_environment.humidity_night_pct = snap->humidity_night_pct;
        state->current_environment.lux_day = snap->lux_day;
        state->current_environment.lux_night = snap->lux_night;
        state->health.hydration_pct = snap->hydration_pct;
        state->health.stress_pct = snap->stress_pct;
        state->health.health_pct = snap->health_pct;
        state->health.last_feeding_timestamp = snap->last_feeding_timestamp;
        state->activity_score = snap->activity_score;
    }

    s_remote_active = count > 0;
    portEXIT_CRITICAL(&s_state_lock);

    ESP_LOGD(TAG, "Applied remote snapshot (%u terrariums, epoch %u)", frame->terrarium_count, (unsigned)frame->epoch_seconds);
    return ESP_OK;
}
