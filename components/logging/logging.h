#ifndef LOGGING_H
#define LOGGING_H

#include "reptile_logic.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature_c;
    float humidity_pct;
    float lux;
    float uva_mw_cm2;
    float uvb_mw_cm2;
    float uv_index;
    bool heating;
    bool pumping;
    bool fan;
    bool uv_lamp;
    bool light;
    bool feeding;
} logging_real_sample_t;

typedef struct {
    const reptile_t *(*get_reptile_state)(void);
    bool (*get_real_sample)(logging_real_sample_t *sample);
    uint32_t period_ms;
} logging_provider_t;

/**
 * @brief Initialize periodic logging.
 */
void logging_init(const logging_provider_t *provider);

/** Pause periodic logging timer. */
void logging_pause(void);

/** Resume periodic logging timer. */
void logging_resume(void);

#ifdef __cplusplus
}
#endif

#endif // LOGGING_H
