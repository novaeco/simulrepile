#include "virtual_sensors.h"

#include "app_config.h"
#include "esp_log.h"

#include <math.h>

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

    float phase = fmodf((float)(state->last_save_timestamp + terrarium_index * 97), 1440.0f) / 1440.0f;
    float day_factor = sinf(phase * 2.0f * (float)M_PI);

    inputs.temperature_c = state->environment.night_temperature_target_c + (state->environment.day_temperature_target_c - state->environment.night_temperature_target_c) * (day_factor * 0.5f + 0.5f);
    inputs.humidity_percent = state->environment.humidity_target_percent + sinf(phase * 4.0f * (float)M_PI) * 5.0f;
    inputs.uv_index = 1.0f + (day_factor * 0.5f + 0.5f) * 5.0f;
    inputs.lux = state->environment.light_night_lux + (state->environment.light_day_lux - state->environment.light_night_lux) * (day_factor * 0.5f + 0.5f);

    ESP_LOGV(TAG, "Terrarium %u virtual sensor T=%.2f H=%.2f", (unsigned)terrarium_index, inputs.temperature_c, inputs.humidity_percent);
    return inputs;
}
