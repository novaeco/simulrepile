#pragma once

#include "sim_models.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature_c;
    float humidity_percent;
    float uv_index;
    float lux;
} virtual_sensor_inputs_t;

virtual_sensor_inputs_t virtual_sensors_sample(size_t terrarium_index, const sim_terrarium_state_t *state);

#ifdef __cplusplus
}
#endif
