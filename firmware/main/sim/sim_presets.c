#include "sim_presets.h"

#include <string.h>

static const sim_species_preset_t s_presets[] = {
    {
        .species_id = "pogona_vitticeps",
        .display_name = "Pogona vitticeps",
        .feeding_interval_days = 2.5f,
        .water_change_interval_days = 1.0f,
        .basking_temp_c = 40.0f,
        .ambient_temp_c = 29.0f,
        .humidity_percent = 40.0f,
    },
    {
        .species_id = "eublepharis_macularius",
        .display_name = "Eublepharis macularius",
        .feeding_interval_days = 3.0f,
        .water_change_interval_days = 2.0f,
        .basking_temp_c = 33.0f,
        .ambient_temp_c = 27.0f,
        .humidity_percent = 45.0f,
    },
};

const sim_species_preset_t *sim_presets_default(size_t *out_count)
{
    if (out_count) {
        *out_count = sizeof(s_presets) / sizeof(s_presets[0]);
    }
    return s_presets;
}

const sim_species_preset_t *sim_presets_get_by_id(const char *species_id)
{
    if (!species_id) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(s_presets) / sizeof(s_presets[0]); ++i) {
        if (strcmp(s_presets[i].species_id, species_id) == 0) {
            return &s_presets[i];
        }
    }
    return NULL;
}
