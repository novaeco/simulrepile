#include "sim_presets.h"

#include <string.h>

static const sim_species_preset_t s_presets[] = {
    {
        .species_id = "pogona_vitticeps",
        .display_name = "Pogona vitticeps",
        .latin_name = "Pogona vitticeps",
        .cites_appendix = "II",
        .captive_status = "NC",
        .basking_temp_c = 40.0f,
        .ambient_temp_c = 29.0f,
        .humidity_percent = 40.0f,
        .feeding_interval_days = 2.5f,
        .water_change_interval_days = 1.0f,
        .supplementation_interval_days = 7.0f,
        .uv_index_day = 4.0f,
        .uv_index_night = 0.5f,
    },
    {
        .species_id = "eublepharis_macularius",
        .display_name = "Eublepharis macularius",
        .latin_name = "Eublepharis macularius",
        .cites_appendix = "II",
        .captive_status = "NC",
        .basking_temp_c = 33.0f,
        .ambient_temp_c = 27.0f,
        .humidity_percent = 45.0f,
        .feeding_interval_days = 3.0f,
        .water_change_interval_days = 2.0f,
        .supplementation_interval_days = 14.0f,
        .uv_index_day = 3.0f,
        .uv_index_night = 0.5f,
    },
    {
        .species_id = "physignathus_cocincinus",
        .display_name = "Physignathus cocincinus",
        .latin_name = "Physignathus cocincinus",
        .cites_appendix = "II",
        .captive_status = "NC",
        .basking_temp_c = 34.0f,
        .ambient_temp_c = 27.0f,
        .humidity_percent = 65.0f,
        .feeding_interval_days = 1.5f,
        .water_change_interval_days = 1.0f,
        .supplementation_interval_days = 10.0f,
        .uv_index_day = 4.5f,
        .uv_index_night = 0.6f,
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
