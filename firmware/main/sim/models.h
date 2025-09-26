#pragma once

#include <stdbool.h>
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

#define TERRARIUM_INVALID_TIMESTAMP UINT32_C(0)

void environment_profile_copy(environment_profile_t *dst, const environment_profile_t *src);
void environment_profile_interpolate(const environment_profile_t *from,
                                     const environment_profile_t *to,
                                     float ratio,
                                     environment_profile_t *out);

void terrarium_state_init(terrarium_state_t *state,
                          const reptile_profile_t *profile,
                          uint32_t timestamp_seconds);
void terrarium_state_set_environment(terrarium_state_t *state, const environment_profile_t *environment);
void terrarium_state_apply_environment(terrarium_state_t *state,
                                       const environment_profile_t *target,
                                       float smoothing_factor);
void terrarium_state_record_feeding(terrarium_state_t *state, uint32_t timestamp_seconds);
uint32_t terrarium_state_time_since_feeding(const terrarium_state_t *state, uint32_t current_timestamp_seconds);
bool terrarium_state_needs_feeding(const terrarium_state_t *state, uint32_t current_timestamp_seconds);

#ifdef __cplusplus
}
#endif
