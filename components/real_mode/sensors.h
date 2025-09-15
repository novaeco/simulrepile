#ifndef REAL_MODE_SENSORS_H
#define REAL_MODE_SENSORS_H

#include "real_mode.h"

esp_err_t sensors_init(const terrarium_hw_t *hw);
esp_err_t sensors_read(const terrarium_hw_t *hw, sensor_data_t *out_data);

#endif /* REAL_MODE_SENSORS_H */
