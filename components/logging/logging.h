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

#ifdef __cplusplus
}
#endif

#endif // LOGGING_H
