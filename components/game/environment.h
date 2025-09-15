#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Day/night environmental profile for a terrarium.
 */
typedef struct {
    float day_temp;      /**< Daytime temperature in Celsius */
    float night_temp;    /**< Nighttime temperature in Celsius */
    float day_humidity;  /**< Daytime relative humidity in percent */
    float night_humidity;/**< Nighttime relative humidity in percent */
    float day_uv;        /**< Daytime UV index */
} env_profile_t;

/** Callback invoked when an environment update occurs. */
typedef void (*environment_update_cb_t)(float temperature,
                                        float humidity,
                                        float uv_index);

/**
 * @brief Register a terrarium to receive periodic environment updates.
 *
 * @param profile           Day/night profile for the terrarium.
 * @param cb                Callback invoked on each update.
 * @param phase_offset_hours Offset in simulated hours for the terrarium's cycle.
 * @return true on success, false if registration capacity is exceeded.
 */
bool environment_register_terrarium(const env_profile_t *profile,
                                    environment_update_cb_t cb,
                                    float phase_offset_hours);

/**
 * @brief Configure the accelerated time scale.
 *
 * @param hours_per_second Simulated hours that elapse for each real second.
 */
void environment_set_time_scale(float hours_per_second);

/**
 * @brief Start accelerated day/night environment simulation.
 */
void environment_init(void);

#endif // ENVIRONMENT_H
