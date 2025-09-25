#include "sensors.h"
#include "game_mode.h"
#include <math.h>
#include "esp_log.h"

extern const sensor_driver_t sensors_real_driver;
extern const sensor_driver_t sensors_sim_driver;

static const sensor_driver_t *s_driver = NULL;
static bool s_using_sim_fallback = false;
static const char *TAG = "sensors";

static void sensors_select_driver(void)
{
    game_mode_t mode = game_mode_get();
    if (mode == GAME_MODE_SIMULATION) {
        if (s_driver != &sensors_sim_driver) {
            s_driver = &sensors_sim_driver;
            s_using_sim_fallback = false;
        }
    } else {
        if (!s_using_sim_fallback && s_driver != &sensors_real_driver) {
            s_driver = &sensors_real_driver;
        }
    }
}

esp_err_t sensors_init(void)
{
    sensors_select_driver();
    if (s_driver && s_driver->init) {
        esp_err_t err = s_driver->init();
        if (err == ESP_ERR_NOT_FOUND && s_driver == &sensors_real_driver) {
            ESP_LOGW(TAG,
                     "No physical sensors detected, enabling simulation fallback");
            s_using_sim_fallback = true;
            const sensor_driver_t *sim_driver = &sensors_sim_driver;
            if (sim_driver->init) {
                esp_err_t sim_err = sim_driver->init();
                if (sim_err != ESP_OK) {
                    ESP_LOGE(TAG,
                             "Simulation fallback failed to initialise: %s",
                             esp_err_to_name(sim_err));
                    return sim_err;
                }
            }
            s_driver = sim_driver;
            return ESP_OK;
        }
        if (err == ESP_OK) {
            s_using_sim_fallback = false;
        }
        return err;
    }
    return ESP_OK;
}

float sensors_read_temperature(void)
{
    sensors_select_driver();
    if (s_driver && s_driver->read_temperature) {
        return s_driver->read_temperature();
    }
    return NAN;
}

float sensors_read_humidity(void)
{
    sensors_select_driver();
    if (s_driver && s_driver->read_humidity) {
        return s_driver->read_humidity();
    }
    return NAN;
}

float sensors_read_lux(void)
{
    sensors_select_driver();
    if (s_driver && s_driver->read_lux) {
        return s_driver->read_lux();
    }
    return NAN;
}

float sensors_read_temperature_channel(size_t channel)
{
    sensors_select_driver();
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
    sensors_select_driver();
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
    sensors_select_driver();
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
    sensors_select_driver();
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
    s_using_sim_fallback = false;
}

bool sensors_is_using_simulation_fallback(void)
{
    return s_using_sim_fallback;
}

