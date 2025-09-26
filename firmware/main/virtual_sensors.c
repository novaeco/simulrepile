#include "virtual_sensors.h"

#include "app_config.h"
#include "esp_log.h"

#include <math.h>

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

static const char *TAG = "virt_sensors";

virtual_sensor_inputs_t virtual_sensors_sample(size_t terrarium_index, const sim_terrarium_state_t *state)
{
    virtual_sensor_inputs_t inputs = {
        .temperature_c = 26.0f,
        .humidity_percent = 50.0f,
        .uv_index = 2.0f,
        .lux = 150.0f,
    };

    if (!state) {
        return inputs;
    }

    float cycle_minutes = (float)state->environment.day_duration_minutes + (float)state->environment.night_duration_minutes;
    if (cycle_minutes <= 0.0f) {
        cycle_minutes = 1440.0f;
    }
    float phase = fmodf(state->environment_elapsed_minutes, cycle_minutes) / cycle_minutes;
    float day_progress = state->active_day_phase
                             ? fmodf(state->environment_elapsed_minutes, (float)state->environment.day_duration_minutes) /
                                   MAX(1.0f, (float)state->environment.day_duration_minutes)
                             : fmodf(state->environment_elapsed_minutes - state->environment.day_duration_minutes,
                                     (float)state->environment.night_duration_minutes) /
                                   MAX(1.0f, (float)state->environment.night_duration_minutes);

    float sinus = sinf((phase + (terrarium_index * 0.13f)) * 2.0f * (float)M_PI) * 0.5f + 0.5f;
    float target_temp = state->environment.night_temperature_target_c +
                        (state->environment.day_temperature_target_c - state->environment.night_temperature_target_c) *
                            (state->active_day_phase ? day_progress : (1.0f - day_progress));

    inputs.temperature_c = target_temp + (sinus - 0.5f) * 2.0f;
    inputs.humidity_percent = state->environment.humidity_target_percent + (0.5f - sinus) * 6.0f;
    inputs.uv_index = state->active_day_phase ? state->environment.uv_index_day * (0.6f + 0.4f * sinus)
                                              : state->environment.uv_index_night * (0.8f + 0.2f * sinus);
    inputs.lux = state->active_day_phase ? state->environment.light_day_lux * (0.7f + 0.3f * sinus)
                                         : state->environment.light_night_lux * (0.8f + 0.2f * sinus);

    ESP_LOGV(TAG, "Terrarium %u virtual sensor T=%.2f H=%.2f", (unsigned)terrarium_index, inputs.temperature_c,
             inputs.humidity_percent);
    return inputs;
}
