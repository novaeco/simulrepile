#ifndef LOGGING_H
#define LOGGING_H

#include "esp_err.h"
#include "reptile_logic.h"
#include "env_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize periodic logging of reptile state to CSV (simulation mode).
 *
 * @param cb Callback returning pointer to current reptile state.
 */
void logging_init(const reptile_facility_t *(*cb)(void));

/** Pause periodic logging timer. */
void logging_pause(void);

/** Resume periodic logging timer. */
void logging_resume(void);

/**
 * @brief Initialize real-mode logging for terrarium automation.
 *
 * @param terrarium_count Number of terrariums handled.
 * @param cfg             Pointer to current environment configuration (names used for files).
 */
esp_err_t logging_real_start(size_t terrarium_count, const reptile_env_config_t *cfg);

/** Append a new real-mode sample to persistent storage. */
void logging_real_append(size_t terrarium_index, const reptile_env_terrarium_state_t *state);

/** Stop real-mode logging and close all files. */
void logging_real_stop(void);

#ifdef __cplusplus
}
#endif

#endif // LOGGING_H
