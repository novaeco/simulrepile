#ifndef LOGGING_H
#define LOGGING_H

#include "reptile_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize periodic logging of reptile state to CSV.
 *
 * @param cb Callback returning pointer to current reptile state.
 */
void logging_init(const reptile_t *(*cb)(void));

/** Pause periodic logging timer. */
void logging_pause(void);

/** Resume periodic logging timer. */
void logging_resume(void);

#ifdef __cplusplus
}
#endif

#endif // LOGGING_H
