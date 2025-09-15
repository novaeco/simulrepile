#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    time_t timestamp;     /**< UNIX timestamp */
    float temperature;    /**< Celsius */
    float humidity;       /**< Percent */
    float uv_index;       /**< UV index */
    float co2;            /**< CO₂ concentration in ppm */
    uint32_t actuator_mask; /**< Bitmask of actuators enabled */
    float power;          /**< Electrical consumption in watts */
} storage_log_entry_t;

typedef enum {
    STORAGE_LOG_CSV,
    STORAGE_LOG_JSON
} storage_log_format_t;

bool storage_append_log(const char *terrarium,
                        const storage_log_entry_t *entry,
                        storage_log_format_t format);

#ifdef __cplusplus
}
#endif
