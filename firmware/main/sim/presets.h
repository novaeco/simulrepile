#pragma once

#include "sim/models.h"

#ifdef __cplusplus
extern "C" {
#endif

const reptile_profile_t *sim_presets_get_default(size_t *count);
const reptile_profile_t *sim_presets_find(const char *scientific_name);

#ifdef __cplusplus
}
#endif
