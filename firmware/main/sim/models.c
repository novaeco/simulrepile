#include "sim/models.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "sim_models";

static float clampf(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

void environment_profile_copy(environment_profile_t *dst, const environment_profile_t *src)
{
    if (!dst || !src) {
        ESP_LOGW(TAG, "environment_profile_copy called with null pointer");
        return;
    }
    memcpy(dst, src, sizeof(environment_profile_t));
}

void environment_profile_interpolate(const environment_profile_t *from,
                                     const environment_profile_t *to,
                                     float ratio,
                                     environment_profile_t *out)
{
    if (!from || !to || !out) {
        ESP_LOGW(TAG, "environment_profile_interpolate called with null pointer");
        return;
    }

    float t = clampf(ratio, 0.0f, 1.0f);
    out->temp_day_c = from->temp_day_c + (to->temp_day_c - from->temp_day_c) * t;
    out->temp_night_c = from->temp_night_c + (to->temp_night_c - from->temp_night_c) * t;
    out->humidity_day_pct = from->humidity_day_pct + (to->humidity_day_pct - from->humidity_day_pct) * t;
    out->humidity_night_pct = from->humidity_night_pct + (to->humidity_night_pct - from->humidity_night_pct) * t;
    out->lux_day = from->lux_day + (to->lux_day - from->lux_day) * t;
    out->lux_night = from->lux_night + (to->lux_night - from->lux_night) * t;
}

void terrarium_state_init(terrarium_state_t *state,
                          const reptile_profile_t *profile,
                          uint32_t timestamp_seconds)
{
    if (!state) {
        ESP_LOGE(TAG, "terrarium_state_init requires a valid state pointer");
        return;
    }

    memset(state, 0, sizeof(*state));
    state->profile = profile;

    if (profile) {
        environment_profile_copy(&state->current_environment, &profile->environment);
    }

    state->health.hydration_pct = 85.0f;
    state->health.stress_pct = 12.0f;
    state->health.health_pct = 95.0f;
    state->health.last_feeding_timestamp = timestamp_seconds != TERRARIUM_INVALID_TIMESTAMP
                                               ? timestamp_seconds
                                               : TERRARIUM_INVALID_TIMESTAMP;
    state->activity_score = 0.5f;
}

void terrarium_state_set_environment(terrarium_state_t *state, const environment_profile_t *environment)
{
    if (!state || !environment) {
        ESP_LOGW(TAG, "terrarium_state_set_environment called with null pointer");
        return;
    }
    environment_profile_copy(&state->current_environment, environment);
}

void terrarium_state_apply_environment(terrarium_state_t *state,
                                       const environment_profile_t *target,
                                       float smoothing_factor)
{
    if (!state || !target) {
        ESP_LOGW(TAG, "terrarium_state_apply_environment called with null pointer");
        return;
    }

    float factor = clampf(smoothing_factor, 0.0f, 1.0f);
    environment_profile_t blended;
    environment_profile_interpolate(&state->current_environment, target, factor, &blended);
    environment_profile_copy(&state->current_environment, &blended);
}

void terrarium_state_record_feeding(terrarium_state_t *state, uint32_t timestamp_seconds)
{
    if (!state) {
        ESP_LOGW(TAG, "terrarium_state_record_feeding called with null state");
        return;
    }
    state->health.last_feeding_timestamp = timestamp_seconds;
    state->health.hydration_pct = clampf(state->health.hydration_pct + 5.0f, 0.0f, 100.0f);
    state->health.stress_pct = clampf(state->health.stress_pct - 3.0f, 0.0f, 100.0f);
}

uint32_t terrarium_state_time_since_feeding(const terrarium_state_t *state, uint32_t current_timestamp_seconds)
{
    if (!state) {
        return 0;
    }
    if (state->health.last_feeding_timestamp == TERRARIUM_INVALID_TIMESTAMP) {
        return 0;
    }
    if (current_timestamp_seconds <= state->health.last_feeding_timestamp) {
        return 0;
    }
    return current_timestamp_seconds - state->health.last_feeding_timestamp;
}

bool terrarium_state_needs_feeding(const terrarium_state_t *state, uint32_t current_timestamp_seconds)
{
    if (!state || !state->profile || state->profile->feeding_interval_days == 0) {
        return false;
    }

    uint32_t elapsed = terrarium_state_time_since_feeding(state, current_timestamp_seconds);
    if (elapsed == 0) {
        return false;
    }

    const uint64_t interval_seconds = (uint64_t)state->profile->feeding_interval_days * 24U * 60U * 60U;
    return elapsed >= interval_seconds;
}
