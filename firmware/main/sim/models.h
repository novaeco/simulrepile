#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temp_day_c;
    float temp_night_c;
    float humidity_day_pct;
    float humidity_night_pct;
    float lux_day;
    float lux_night;
} environment_profile_t;

typedef struct {
    const char *scientific_name;
    const char *common_name;
    environment_profile_t environment;
    uint8_t feeding_interval_days;
} reptile_profile_t;

typedef struct {
    float hydration_pct;
    float stress_pct;
    float health_pct;
    uint32_t last_feeding_timestamp;
} health_state_t;

typedef struct {
    const reptile_profile_t *profile;
    environment_profile_t current_environment;
    health_state_t health;
    float activity_score;
} terrarium_state_t;

#ifdef __cplusplus
}
#endif
