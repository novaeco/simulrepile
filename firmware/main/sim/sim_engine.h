#pragma once

#include "sim_models.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*sim_engine_event_cb_t)(const sim_terrarium_state_t *state, void *user_ctx);

void sim_engine_init(void);
void sim_engine_tick(uint32_t elapsed_ms);
size_t sim_engine_terrarium_count(void);
const sim_terrarium_state_t *sim_engine_get_state(size_t index);
int sim_engine_add_terrarium(const sim_species_preset_t *preset, const char *nickname);
int sim_engine_attach_state(const sim_terrarium_state_t *snapshot);
void sim_engine_register_event_callback(sim_engine_event_cb_t cb, void *user_ctx);
void sim_engine_set_environment(size_t index, const sim_environment_profile_t *profile);
void sim_engine_feed(size_t index, const char *notes);
void sim_engine_mist(size_t index, const char *notes);
void sim_engine_mark_manual_care(size_t index, const char *description);

#ifdef __cplusplus
}
#endif
