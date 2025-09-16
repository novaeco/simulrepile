#include <stdio.h>
#include "game_mode.h"
#include "sensors.h"
#include "gpio.h"
#include "sim_api.h"

int main(void)
{
#ifdef GAME_MODE_SIMULATION
    game_mode_set(GAME_MODE_SIMULATION);
    sensors_init();
    reptile_actuators_init();

    sensors_sim_set_temperature(32.5f);
    sensors_sim_set_humidity(65.0f);

    float t = sensors_read_temperature();
    float h = sensors_read_humidity();
    printf("Temp=%.1fC Hum=%.1f%%\n", t, h);

    DEV_Digital_Write(HEAT_RES_PIN, 1);
    DEV_Digital_Write(WATER_PUMP_PIN, 1);
    printf("Heater=%d Pump=%d\n", gpio_sim_get_heater_state(), gpio_sim_get_pump_state());

    reptile_actuators_deinit();
    sensors_deinit();
#else
    printf("Simulation mode not enabled.\n");
#endif
    return 0;
}
