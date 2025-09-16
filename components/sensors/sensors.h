#ifndef SENSORS_H
#define SENSORS_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t (*init)(void);
    float (*read_temperature)(void);
    float (*read_humidity)(void);
    void (*deinit)(void);
} sensor_driver_t;

esp_err_t sensors_init(void);
float sensors_read_temperature(void);
float sensors_read_humidity(void);
void sensors_deinit(void);


#ifdef __cplusplus
}
#endif

#endif // SENSORS_H
