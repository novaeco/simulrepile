#ifndef REAL_MODE_ACTUATORS_H
#define REAL_MODE_ACTUATORS_H

#include "real_mode.h"

esp_err_t actuators_init(const terrarium_hw_t *hw);
esp_err_t actuators_apply(const terrarium_hw_t *hw, const sensor_data_t *data, const real_mode_state_t *state);

#endif /* REAL_MODE_ACTUATORS_H */
