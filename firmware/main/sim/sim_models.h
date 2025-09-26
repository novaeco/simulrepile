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
    float body_condition_score;
    uint32_t wellness_flags;
} sim_health_state_t;

typedef struct {
    float day_temperature_target_c;
    float night_temperature_target_c;
    float humidity_target_percent;
    float uv_index_day;
    float uv_index_night;
    float light_day_lux;
    float light_night_lux;
    uint32_t day_duration_minutes;
    uint32_t night_duration_minutes;
    uint32_t season_length_days;
    float seasonal_temp_shift_c;
    float seasonal_humidity_shift_percent;
} sim_environment_profile_t;

typedef struct {
    float enclosure_length_cm;
    float enclosure_width_cm;
    float enclosure_height_cm;
    char substrate[32];
    bool bioactive;
} sim_habitat_profile_t;

typedef struct {
    char species_id[32];
    char display_name[64];
    char latin_name[64];
    char cites_appendix[8];
    char captive_status[16];
    float basking_temp_c;
    float ambient_temp_c;
    float humidity_percent;
    float feeding_interval_days;
    float water_change_interval_days;
    float supplementation_interval_days;
    float uv_index_day;
    float uv_index_night;
} sim_species_preset_t;

typedef struct {
    float weight_grams;
    float growth_rate_g_per_day;
    float hydration_ml_per_day;
    float feeding_interval_days;
    float supplementation_interval_days;
    uint32_t last_feeding_timestamp;
    uint32_t last_supplement_timestamp;
    uint32_t last_mist_timestamp;
} sim_nutrition_state_t;

typedef struct {
    char entry_id[32];
    char timestamp_iso8601[32];
    char description[128];
    char category[32];
} sim_care_entry_t;

typedef struct {
    char terrarium_id[32];
    char nickname[32];
    sim_species_preset_t species;
    sim_environment_profile_t environment;
    sim_habitat_profile_t habitat;
    sim_health_state_t health;
    sim_nutrition_state_t nutrition;
    sim_care_entry_t care_history[32];
    uint8_t care_history_count;
    uint32_t care_history_total;
    uint32_t last_save_timestamp;
    float environment_elapsed_minutes;
    bool active_day_phase;
} sim_terrarium_state_t;

#endif
