#pragma once

#include <stddef.h>

#include "sim/models.h"

#ifdef __cplusplus
extern "C" {
#endif

void sim_engine_init(void);
void sim_engine_step(float delta_seconds);
size_t sim_engine_get_count(void);
const terrarium_state_t *sim_engine_get_state(size_t index);

#ifdef __cplusplus
}
#endif
