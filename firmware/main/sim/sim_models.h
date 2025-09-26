#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float temperature_c;
    float humidity_percent;
    float uv_index;
    float illumination_lux;
    float hydration_level;
    float stress_level;
    float shedding_progress;
    float hunger_level;
    float activity_level;
    float hideout_usage;
} sim_health_state_t;

typedef struct {
    float day_temperature_target_c;
    float night_temperature_target_c;
    float humidity_target_percent;
    float light_day_lux;
    float light_night_lux;
    uint32_t day_duration_minutes;
    uint32_t night_duration_minutes;
    uint32_t season_length_days;
} sim_environment_profile_t;

typedef struct {
    char species_id[32];
    char display_name[64];
    float feeding_interval_days;
    float water_change_interval_days;
    float basking_temp_c;
    float ambient_temp_c;
    float humidity_percent;
} sim_species_preset_t;

typedef struct {
    char entry_id[32];
    char timestamp_iso8601[32];
    char description[128];
} sim_care_entry_t;

typedef struct {
    char terrarium_id[32];
    char nickname[32];
    sim_environment_profile_t environment;
    sim_health_state_t health;
    sim_species_preset_t species;
    sim_care_entry_t care_history[16];
    uint8_t care_history_count;
    uint32_t last_feeding_timestamp;
    uint32_t last_mist_timestamp;
    uint32_t last_save_timestamp;
} sim_terrarium_state_t;

#endif
