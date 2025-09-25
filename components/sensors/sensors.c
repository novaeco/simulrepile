#include "sensors.h"
#include <math.h>
#include "esp_log.h"

extern const sensor_driver_t sensors_sim_driver;

static const sensor_driver_t *s_driver = NULL;
static const char *TAG = "sensors";

esp_err_t sensors_init(void)
{
    if (!s_driver) {
        s_driver = &sensors_sim_driver;
    }
    if (s_driver && s_driver->init) {
        return s_driver->init();
    }
    return ESP_OK;
}

float sensors_read_temperature(void)
{
    if (s_driver && s_driver->read_temperature) {
        return s_driver->read_temperature();
    }
    return NAN;
}

float sensors_read_humidity(void)
{
    if (s_driver && s_driver->read_humidity) {
        return s_driver->read_humidity();
    }
    return NAN;
}

float sensors_read_lux(void)
{
    if (s_driver && s_driver->read_lux) {
        return s_driver->read_lux();
    }
    return NAN;
}

float sensors_read_temperature_channel(size_t channel)
{
    if (!s_driver) {
        return NAN;
    }
    if (s_driver->read_temperature_channel) {
        return s_driver->read_temperature_channel(channel);
    }
    if (channel == 0 && s_driver->read_temperature) {
        return s_driver->read_temperature();
    }
    return NAN;
}

float sensors_read_humidity_channel(size_t channel)
{
    if (!s_driver) {
        return NAN;
    }
    if (s_driver->read_humidity_channel) {
        return s_driver->read_humidity_channel(channel);
    }
    if (channel == 0 && s_driver->read_humidity) {
        return s_driver->read_humidity();
    }
    return NAN;
}

float sensors_read_lux_channel(size_t channel)
{
    if (!s_driver) {
        return NAN;
    }
    if (s_driver->read_lux_channel) {
        return s_driver->read_lux_channel(channel);
    }
    if (channel == 0 && s_driver->read_lux) {
        return s_driver->read_lux();
    }
    return NAN;
}

size_t sensors_get_channel_count(void)
{
    if (s_driver && s_driver->get_channel_count) {
        return s_driver->get_channel_count();
    }
    return (s_driver && (s_driver->read_temperature || s_driver->read_humidity)) ? 1u : 0u;
}

void sensors_deinit(void)
{
    if (s_driver && s_driver->deinit) {
        s_driver->deinit();
    }
    s_driver = NULL;
}

bool sensors_is_using_simulation_fallback(void)
{
    return false;
}

