#include "sim/presets.h"

#include <string.h>

static const reptile_profile_t s_presets[] = {
    {
        .scientific_name = "Python regius",
        .common_name = "Python royal",
        .environment = {
            .temp_day_c = 31.0f,
            .temp_night_c = 26.0f,
            .humidity_day_pct = 60.0f,
            .humidity_night_pct = 70.0f,
            .lux_day = 350.0f,
            .lux_night = 5.0f,
        },
        .feeding_interval_days = 8,
    },
    {
        .scientific_name = "Pogona vitticeps",
        .common_name = "Dragon barbu",
        .environment = {
            .temp_day_c = 36.0f,
            .temp_night_c = 22.0f,
            .humidity_day_pct = 40.0f,
            .humidity_night_pct = 45.0f,
            .lux_day = 500.0f,
            .lux_night = 10.0f,
        },
        .feeding_interval_days = 3,
    },
};

const reptile_profile_t *sim_presets_get_default(size_t *count)
{
    if (count) {
        *count = sizeof(s_presets) / sizeof(s_presets[0]);
    }
    return s_presets;
}

const reptile_profile_t *sim_presets_find(const char *scientific_name)
{
    if (!scientific_name) {
        return NULL;
    }
    size_t count = 0;
    const reptile_profile_t *presets = sim_presets_get_default(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(presets[i].scientific_name, scientific_name) == 0) {
            return &presets[i];
        }
    }
    return NULL;
}
