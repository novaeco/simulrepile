#pragma once

#include <stdbool.h>

#ifdef GAME_MODE_SIMULATION

void sensors_sim_set_temperature(float temp);
void sensors_sim_set_humidity(float hum);

bool gpio_sim_get_heater_state(void);
bool gpio_sim_get_pump_state(void);

#endif // GAME_MODE_SIMULATION

