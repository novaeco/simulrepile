#include "sensors.h"
#include "game_mode.h"

extern const sensor_driver_t sensors_real_driver;
extern const sensor_driver_t sensors_sim_driver;

static const sensor_driver_t *s_driver = NULL;

static void sensors_select_driver(void)
{
    if (!s_driver) {
        s_driver = (game_mode_get() == GAME_MODE_SIMULATION) ?
                       &sensors_sim_driver : &sensors_real_driver;
    }
}

esp_err_t sensors_init(void)
{
    sensors_select_driver();
    if (s_driver && s_driver->init) {
        return s_driver->init();
    }
    return ESP_OK;
}

float sensors_read_temperature(void)
{
    sensors_select_driver();
    if (s_driver && s_driver->read_temperature) {
        return s_driver->read_temperature();
    }
    return 0.0f;
}

float sensors_read_humidity(void)
{
    sensors_select_driver();
    if (s_driver && s_driver->read_humidity) {
        return s_driver->read_humidity();
    }
    return 0.0f;
}

void sensors_deinit(void)
{
    if (s_driver && s_driver->deinit) {
        s_driver->deinit();
    }
    s_driver = NULL;
}

