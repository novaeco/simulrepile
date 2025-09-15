#ifndef REAL_MODE_LOGGING_H
#define REAL_MODE_LOGGING_H

#include "real_mode.h"

esp_err_t logging_init(void);
esp_err_t logging_write(const sensor_data_t *data);

#endif /* REAL_MODE_LOGGING_H */
