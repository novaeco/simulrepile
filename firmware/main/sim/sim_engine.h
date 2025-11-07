#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "link/core_link_protocol.h"
#include "sim/models.h"

#ifdef __cplusplus
extern "C" {
#endif

void sim_engine_init(void);
void sim_engine_step(float delta_seconds);
size_t sim_engine_get_count(void);
const terrarium_state_t *sim_engine_get_state(size_t index);
esp_err_t sim_engine_apply_remote_snapshot(const core_link_state_frame_t *frame);
void sim_engine_handle_link_status(bool connected);

#ifdef __cplusplus
}
#endif
