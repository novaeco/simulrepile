#include "sim/sim_engine.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "i18n/i18n_manager.h"
#include "link/core_link_protocol.h"
#include "sim/presets.h"

#define MAX_TERRARIUMS 4
#define SIM_TIME_ACCELERATION 240.0f
#define SIM_SECONDS_PER_DAY (24.0f * 60.0f * 60.0f)
#define SIM_SEASON_LENGTH_DAYS 120.0f

typedef struct {
    float circadian_phase;
    float season_phase;
    float hydration_reservoir;
    float stress_trend;
} sim_runtime_state_t;

static const char *TAG = "sim_engine";
static terrarium_state_t s_terrariums[MAX_TERRARIUMS];
static size_t s_terrarium_count = 0;
static float s_time_accumulator = 0.0f;
static double s_simulated_seconds = 0.0;
static bool s_remote_active = false;
static bool s_watchdog_fault_latched = false;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static const reptile_profile_t *s_default_profiles[MAX_TERRARIUMS];
static reptile_profile_t s_remote_profiles[MAX_TERRARIUMS];
static reptile_profile_t s_manual_profiles[MAX_TERRARIUMS];
static char s_remote_scientific_names[MAX_TERRARIUMS][CORE_LINK_NAME_MAX_LEN + 1];
static char s_remote_common_names[MAX_TERRARIUMS][CORE_LINK_NAME_MAX_LEN + 1];
static char s_manual_scientific_names[MAX_TERRARIUMS][CORE_LINK_NAME_MAX_LEN + 1];
static char s_manual_common_names[MAX_TERRARIUMS][CORE_LINK_NAME_MAX_LEN + 1];
static sim_runtime_state_t s_runtime[MAX_TERRARIUMS];

static void sim_engine_load_defaults_locked(void);
static void sim_engine_reset_manual_profile(size_t index);
static void sim_engine_reset_runtime(size_t index);
static void sim_engine_sync_runtime_from_state(size_t index, const terrarium_state_t *state);
static void sim_engine_update_local_slot(size_t index, float scaled_delta, uint32_t now_seconds);
static float sim_clampf(float value, float min_value, float max_value);

void sim_engine_init(void)
{
    portENTER_CRITICAL(&s_state_lock);
    memset(s_runtime, 0, sizeof(s_runtime));
    memset(s_terrariums, 0, sizeof(s_terrariums));
    memset(s_remote_profiles, 0, sizeof(s_remote_profiles));
    memset(s_manual_profiles, 0, sizeof(s_manual_profiles));
    memset(s_remote_scientific_names, 0, sizeof(s_remote_scientific_names));
    memset(s_remote_common_names, 0, sizeof(s_remote_common_names));
    memset(s_manual_scientific_names, 0, sizeof(s_manual_scientific_names));
    memset(s_manual_common_names, 0, sizeof(s_manual_common_names));
    s_remote_active = false;
    s_time_accumulator = 0.0f;
    s_simulated_seconds = 0.0;
    sim_engine_load_defaults_locked();
    portEXIT_CRITICAL(&s_state_lock);
    ESP_LOGI(TAG, "Simulation initialized with %d terrariums", (int)s_terrarium_count);
}

static float sim_clampf(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void sim_engine_load_defaults_locked(void)
{
    size_t preset_count = 0;
    const reptile_profile_t *presets = sim_presets_get_default(&preset_count);
    size_t count = preset_count < MAX_TERRARIUMS ? preset_count : MAX_TERRARIUMS;
    s_terrarium_count = count;
    uint32_t now = (uint32_t)s_simulated_seconds;

    for (size_t i = 0; i < count; ++i) {
        s_default_profiles[i] = &presets[i];
        sim_engine_reset_manual_profile(i);
        terrarium_state_t *state = &s_terrariums[i];
        terrarium_state_init(state, s_default_profiles[i], now);
        sim_engine_sync_runtime_from_state(i, state);
    }

    for (size_t i = count; i < MAX_TERRARIUMS; ++i) {
        s_default_profiles[i] = NULL;
        sim_engine_reset_manual_profile(i);
        memset(&s_terrariums[i], 0, sizeof(s_terrariums[i]));
        sim_engine_reset_runtime(i);
    }
}

static void sim_engine_reset_manual_profile(size_t index)
{
    if (index >= MAX_TERRARIUMS) {
        return;
    }
    memset(&s_manual_profiles[index], 0, sizeof(s_manual_profiles[index]));
    memset(s_manual_scientific_names[index], 0, sizeof(s_manual_scientific_names[index]));
    memset(s_manual_common_names[index], 0, sizeof(s_manual_common_names[index]));
    sim_engine_reset_runtime(index);
}

static void sim_engine_reset_runtime(size_t index)
{
    if (index >= MAX_TERRARIUMS) {
        return;
    }
    sim_runtime_state_t *runtime = &s_runtime[index];
    float offset = (float)(index + 1U) / (float)(MAX_TERRARIUMS + 1U);
    runtime->circadian_phase = sim_clampf(offset, 0.0f, 1.0f);
    runtime->season_phase = sim_clampf(offset * 0.37f, 0.0f, 1.0f);
    runtime->hydration_reservoir = 0.75f;
    runtime->stress_trend = 0.15f;
}

static void sim_engine_sync_runtime_from_state(size_t index, const terrarium_state_t *state)
{
    if (index >= MAX_TERRARIUMS || !state) {
        return;
    }
    sim_runtime_state_t *runtime = &s_runtime[index];
    runtime->hydration_reservoir = sim_clampf(state->health.hydration_pct / 100.0f, 0.0f, 1.0f);
    runtime->stress_trend = sim_clampf(state->health.stress_pct / 100.0f, 0.0f, 1.0f);
}

static void sim_engine_update_local_slot(size_t index, float scaled_delta, uint32_t now_seconds)
{
    terrarium_state_t *state = &s_terrariums[index];
    if (!state->profile) {
        return;
    }

    sim_runtime_state_t *runtime = &s_runtime[index];
    float day_increment = scaled_delta / SIM_SECONDS_PER_DAY;
    runtime->circadian_phase += day_increment;
    runtime->circadian_phase -= floorf(runtime->circadian_phase);
    float season_increment = scaled_delta / (SIM_SECONDS_PER_DAY * SIM_SEASON_LENGTH_DAYS);
    runtime->season_phase += season_increment;
    runtime->season_phase -= floorf(runtime->season_phase);

    float circadian = 0.5f - 0.5f * cosf(runtime->circadian_phase * 2.0f * (float)M_PI);
    float seasonal = sinf(runtime->season_phase * 2.0f * (float)M_PI);
    float micro = sinf((s_time_accumulator * 0.05f) + (float)index * 0.8f);

    environment_profile_t target = state->profile->environment;
    float temp_span = state->profile->environment.temp_day_c - state->profile->environment.temp_night_c;
    target.temp_day_c = state->profile->environment.temp_night_c + temp_span * circadian + seasonal * 1.6f + micro * 0.8f;
    float humidity_span = state->profile->environment.humidity_day_pct - state->profile->environment.humidity_night_pct;
    target.humidity_day_pct = state->profile->environment.humidity_night_pct + humidity_span * circadian + seasonal * 4.0f;
    target.humidity_day_pct = sim_clampf(target.humidity_day_pct, 30.0f, 95.0f);
    float lux_span = state->profile->environment.lux_day - state->profile->environment.lux_night;
    target.lux_day = state->profile->environment.lux_night + lux_span * circadian;
    target.lux_day += state->profile->environment.lux_day * 0.05f * micro;
    if (target.lux_day < state->profile->environment.lux_night) {
        target.lux_day = state->profile->environment.lux_night;
    }

    float smoothing = sim_clampf(scaled_delta / 3600.0f, 0.05f, 1.0f);
    terrarium_state_apply_environment(state, &target, smoothing);

    float humidity_norm = sim_clampf((state->current_environment.humidity_day_pct - 40.0f) / 60.0f, 0.0f, 1.0f);
    float hydration_rate = sim_clampf(scaled_delta / 7200.0f, 0.05f, 0.35f);
    runtime->hydration_reservoir += (humidity_norm - runtime->hydration_reservoir) * hydration_rate;
    runtime->hydration_reservoir = sim_clampf(runtime->hydration_reservoir, 0.0f, 1.0f);
    state->health.hydration_pct = sim_clampf(55.0f + runtime->hydration_reservoir * 45.0f, 25.0f, 100.0f);

    float temp_error = fabsf(state->current_environment.temp_day_c - state->profile->environment.temp_day_c);
    float humidity_error = fabsf(state->current_environment.humidity_day_pct - state->profile->environment.humidity_day_pct);
    float lux_target = state->profile->environment.lux_day > 1.0f ? state->profile->environment.lux_day : 1.0f;
    float lux_error = fabsf(state->current_environment.lux_day - state->profile->environment.lux_day) / lux_target;
    float environment_penalty = temp_error * 1.35f + humidity_error * 0.32f + lux_error * 22.0f;

    uint32_t elapsed = terrarium_state_time_since_feeding(state, now_seconds);
    float feeding_penalty = 0.0f;
    if (state->profile->feeding_interval_days > 0U) {
        float interval = (float)state->profile->feeding_interval_days * 24.0f * 3600.0f;
        if (interval > 0.0f && (float)elapsed > interval) {
            float overdue = (float)elapsed - interval;
            feeding_penalty = sim_clampf((overdue / interval) * 60.0f, 0.0f, 45.0f);
        }
    }

    float hydration_penalty = sim_clampf((80.0f - state->health.hydration_pct) * 0.45f, 0.0f, 35.0f);
    float stress_target = sim_clampf(12.0f + environment_penalty + feeding_penalty * 0.5f + hydration_penalty * 0.6f, 0.0f, 100.0f);
    float stress_rate = sim_clampf(scaled_delta / 5400.0f, 0.05f, 0.4f);
    runtime->stress_trend += ((stress_target / 100.0f) - runtime->stress_trend) * stress_rate;
    runtime->stress_trend = sim_clampf(runtime->stress_trend, 0.0f, 1.0f);
    state->health.stress_pct = sim_clampf(runtime->stress_trend * 100.0f, 0.0f, 100.0f);

    float health_target = sim_clampf(100.0f - (environment_penalty * 0.4f + feeding_penalty + hydration_penalty), 15.0f, 100.0f);
    float health_rate = sim_clampf(scaled_delta / 7200.0f, 0.03f, 0.25f);
    state->health.health_pct += (health_target - state->health.health_pct) * health_rate;
    state->health.health_pct = sim_clampf(state->health.health_pct, 0.0f, 100.0f);

    float temp_norm = 1.0f - sim_clampf(temp_error / 12.0f, 0.0f, 1.0f);
    float stress_norm = 1.0f - state->health.stress_pct / 100.0f;
    float hydration_norm = sim_clampf(state->health.hydration_pct / 100.0f, 0.0f, 1.0f);
    float activity_target = sim_clampf(0.18f + 0.55f * temp_norm + 0.17f * stress_norm + 0.10f * hydration_norm, 0.05f, 0.98f);
    float activity_rate = sim_clampf(scaled_delta / 3600.0f, 0.04f, 0.35f);
    state->activity_score += (activity_target - state->activity_score) * activity_rate;
    state->activity_score = sim_clampf(state->activity_score, 0.0f, 1.0f);
}

void sim_engine_step(float delta_seconds)
{
    if (s_remote_active) {
        (void)delta_seconds;
        return;
    }
    if (delta_seconds <= 0.0f) {
        return;
    }
    portENTER_CRITICAL(&s_state_lock);
    float scaled_delta = delta_seconds * SIM_TIME_ACCELERATION;
    s_time_accumulator += scaled_delta;
    s_simulated_seconds += scaled_delta;
    uint32_t now_seconds = (uint32_t)s_simulated_seconds;
    for (size_t i = 0; i < s_terrarium_count; ++i) {
        sim_engine_update_local_slot(i, scaled_delta, now_seconds);
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

esp_err_t sim_engine_export_slot(size_t index, sim_saved_slot_t *out_state)
{
    if (!out_state) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t status = ESP_OK;
    portENTER_CRITICAL(&s_state_lock);
    if (index >= s_terrarium_count) {
        status = ESP_ERR_NOT_FOUND;
    } else {
        const terrarium_state_t *state = &s_terrariums[index];
        if (!state->profile) {
            status = ESP_ERR_INVALID_STATE;
        } else {
            memset(out_state, 0, sizeof(*out_state));
            if (state->profile->scientific_name) {
                strlcpy(out_state->scientific_name,
                        state->profile->scientific_name,
                        sizeof(out_state->scientific_name));
            }
            if (state->profile->common_name) {
                strlcpy(out_state->common_name,
                        state->profile->common_name,
                        sizeof(out_state->common_name));
            }
            out_state->environment = state->current_environment;
            out_state->health = state->health;
            out_state->activity_score = state->activity_score;
            out_state->feeding_interval_days = state->profile->feeding_interval_days;
        }
    }
    portEXIT_CRITICAL(&s_state_lock);
    return status;
}

esp_err_t sim_engine_restore_slot(size_t index, const sim_saved_slot_t *state)
{
    if (!state || index >= MAX_TERRARIUMS) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_state_lock);

    terrarium_state_t *slot = &s_terrariums[index];
    const reptile_profile_t *profile = sim_presets_find(state->scientific_name);
    if (profile) {
        slot->profile = profile;
    } else {
        sim_engine_reset_manual_profile(index);
        strlcpy(s_manual_scientific_names[index], state->scientific_name, sizeof(s_manual_scientific_names[index]));
        strlcpy(s_manual_common_names[index], state->common_name, sizeof(s_manual_common_names[index]));
        s_manual_profiles[index].scientific_name = s_manual_scientific_names[index];
        s_manual_profiles[index].common_name = s_manual_common_names[index];
        s_manual_profiles[index].environment = state->environment;
        s_manual_profiles[index].feeding_interval_days = state->feeding_interval_days;
        slot->profile = &s_manual_profiles[index];
    }

    slot->current_environment = state->environment;
    slot->health = state->health;
    slot->activity_score = state->activity_score;
    sim_engine_sync_runtime_from_state(index, slot);

    if (index >= s_terrarium_count) {
        s_terrarium_count = index + 1;
    }
    s_remote_active = false;
    portEXIT_CRITICAL(&s_state_lock);

    ESP_LOGI(TAG,
             "Slot %u restored (profile=%s)",
             (unsigned)(index + 1),
             state->scientific_name[0] ? state->scientific_name : "unknown");
    return ESP_OK;
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
        sim_engine_reset_manual_profile(i);
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
        sim_engine_sync_runtime_from_state(i, state);
    }

    for (size_t i = count; i < MAX_TERRARIUMS; ++i) {
        sim_engine_reset_manual_profile(i);
        memset(&s_remote_profiles[i], 0, sizeof(s_remote_profiles[i]));
        memset(s_remote_scientific_names[i], 0, sizeof(s_remote_scientific_names[i]));
        memset(s_remote_common_names[i], 0, sizeof(s_remote_common_names[i]));
        memset(&s_terrariums[i], 0, sizeof(s_terrariums[i]));
    }

    s_remote_active = count > 0;
    if (frame->epoch_seconds != 0U) {
        s_simulated_seconds = frame->epoch_seconds;
        s_time_accumulator = (float)s_simulated_seconds;
    }
    portEXIT_CRITICAL(&s_state_lock);

    ESP_LOGD(TAG,
             "Applied remote snapshot (%u terrariums, epoch %u)",
             frame->terrarium_count,
             (unsigned)frame->epoch_seconds);
    return ESP_OK;
}

void sim_engine_hint_remote_count(size_t count)
{
    if (count > MAX_TERRARIUMS) {
        count = MAX_TERRARIUMS;
    }

    portENTER_CRITICAL(&s_state_lock);
    size_t previous = s_terrarium_count;
    if (count != previous) {
        if (count < previous) {
            for (size_t i = count; i < previous; ++i) {
                sim_engine_reset_manual_profile(i);
                memset(&s_remote_profiles[i], 0, sizeof(s_remote_profiles[i]));
                memset(s_remote_scientific_names[i], 0, sizeof(s_remote_scientific_names[i]));
                memset(s_remote_common_names[i], 0, sizeof(s_remote_common_names[i]));
                memset(&s_terrariums[i], 0, sizeof(s_terrariums[i]));
            }
        } else {
            for (size_t i = previous; i < count; ++i) {
                sim_engine_reset_manual_profile(i);
                memset(&s_remote_profiles[i], 0, sizeof(s_remote_profiles[i]));
                memset(s_remote_scientific_names[i], 0, sizeof(s_remote_scientific_names[i]));
                memset(s_remote_common_names[i], 0, sizeof(s_remote_common_names[i]));
                memset(&s_terrariums[i], 0, sizeof(s_terrariums[i]));
                s_terrariums[i].profile = NULL;
            }
        }
        s_terrarium_count = count;
    }
    if (count == 0) {
        s_remote_active = false;
    }
    portEXIT_CRITICAL(&s_state_lock);

    ESP_LOGI(TAG, "Terrarium count hint updated to %u", (unsigned)count);
}

const char *sim_engine_handle_link_status(bool connected)
{
    const char *alert = NULL;
    bool restored = false;
    portENTER_CRITICAL(&s_state_lock);
    if (!connected) {
        s_remote_active = false;
        s_time_accumulator = 0.0f;
        memset(s_remote_profiles, 0, sizeof(s_remote_profiles));
        memset(s_remote_scientific_names, 0, sizeof(s_remote_scientific_names));
        memset(s_remote_common_names, 0, sizeof(s_remote_common_names));
        sim_engine_load_defaults_locked();
        alert = i18n_manager_get_string("alert_link_lost");
        s_watchdog_fault_latched = true;
    } else if (s_watchdog_fault_latched) {
        alert = i18n_manager_get_string("alert_link_restored");
        s_watchdog_fault_latched = false;
        restored = true;
    }
    portEXIT_CRITICAL(&s_state_lock);

    if (connected) {
        ESP_LOGI(TAG,
                 "Core link available, awaiting remote state updates%s",
                 restored ? " (resync pending)" : "");
    } else {
        ESP_LOGW(TAG, "Core link lost, resuming internal terrarium simulation");
    }
    return alert;
}
