#ifndef SENSORS_H
#define SENSORS_H

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t (*init)(void);
    float (*read_temperature)(void);
    float (*read_humidity)(void);
    float (*read_lux)(void);
    void (*deinit)(void);
    size_t (*get_channel_count)(void);
    float (*read_temperature_channel)(size_t channel);
    float (*read_humidity_channel)(size_t channel);
    float (*read_lux_channel)(size_t channel);
} sensor_driver_t;

esp_err_t sensors_init(void);
float sensors_read_temperature(void);
float sensors_read_humidity(void);
float sensors_read_lux(void);
float sensors_read_temperature_channel(size_t channel);
float sensors_read_humidity_channel(size_t channel);
float sensors_read_lux_channel(size_t channel);
size_t sensors_get_channel_count(void);
void sensors_deinit(void);
bool sensors_is_using_simulation_fallback(void);

#ifdef __cplusplus
}
#endif

#endif // SENSORS_H
