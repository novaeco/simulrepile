#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "link/core_link_protocol.h"
#include "sim/models.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char scientific_name[CORE_LINK_NAME_MAX_LEN + 1];
    char common_name[CORE_LINK_NAME_MAX_LEN + 1];
    environment_profile_t environment;
    health_state_t health;
    float activity_score;
    uint8_t feeding_interval_days;
} sim_saved_slot_t;

void sim_engine_init(void);
void sim_engine_step(float delta_seconds);
size_t sim_engine_get_count(void);
const terrarium_state_t *sim_engine_get_state(size_t index);
esp_err_t sim_engine_export_slot(size_t index, sim_saved_slot_t *out_state);
esp_err_t sim_engine_restore_slot(size_t index, const sim_saved_slot_t *state);
esp_err_t sim_engine_apply_remote_snapshot(const core_link_state_frame_t *frame);
const char *sim_engine_handle_link_status(bool connected);
void sim_engine_hint_remote_count(size_t count);

#ifdef __cplusplus
}
#endif
