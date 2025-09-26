#pragma once

#include "sim_models.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const sim_species_preset_t *sim_presets_default(size_t *out_count);
const sim_species_preset_t *sim_presets_get_by_id(const char *species_id);

#ifdef __cplusplus
}
#endif
