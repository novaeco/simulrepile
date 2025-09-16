#ifndef SENSORS_H
#define SENSORS_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float uva;
    float uvb;
    float uv_index;
} sensor_uv_data_t;

typedef struct {
    esp_err_t (*init)(void);
    float (*read_temperature)(void);
    float (*read_humidity)(void);
    float (*read_lux)(void);
    sensor_uv_data_t (*read_uv)(void);
    void (*deinit)(void);
} sensor_driver_t;

esp_err_t sensors_init(void);
float sensors_read_temperature(void);
float sensors_read_humidity(void);
float sensors_read_lux(void);
sensor_uv_data_t sensors_read_uv(void);
void sensors_deinit(void);


#ifdef __cplusplus
}
#endif

#endif // SENSORS_H
