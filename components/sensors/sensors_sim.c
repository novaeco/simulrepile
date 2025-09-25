#include "sensors.h"
#include "esp_random.h"
#include <math.h>

static float s_temp = NAN;
static float s_hum = NAN;
static float s_lux = NAN;

static esp_err_t sensors_sim_init(void)
{
    return ESP_OK;
}

static float sensors_sim_read_temperature(void)
{
    if (!isnan(s_temp)) {
        return s_temp;
    }
    uint32_t randv = esp_random();
    return 26.0f + (float)(randv % 80) / 10.0f;
}

static float sensors_sim_read_humidity(void)
{
    if (!isnan(s_hum)) {
        return s_hum;
    }
    uint32_t randv = esp_random();
    return 40.0f + (float)(randv % 200) / 10.0f;
}

static float sensors_sim_read_lux(void)
{
    if (!isnan(s_lux)) {
        return s_lux;
    }
    uint32_t randv = esp_random();
    return 120.0f + (float)(randv % 800) / 4.0f; // 120 to ~320 lux
}

static void sensors_sim_deinit(void)
{
    s_temp = NAN;
    s_hum = NAN;
    s_lux = NAN;
}

static size_t sensors_sim_channel_count(void)
{
    return 4; // simulate four channels by default
}

static float sensors_sim_read_temperature_channel(size_t channel)
{
    (void)channel;
    return sensors_sim_read_temperature();
}

static float sensors_sim_read_humidity_channel(size_t channel)
{
    (void)channel;
    return sensors_sim_read_humidity();
}

static float sensors_sim_read_lux_channel(size_t channel)
{
    (void)channel;
    return sensors_sim_read_lux();
}

void sensors_sim_set_temperature(float temp)
{
    s_temp = temp;
}

void sensors_sim_set_humidity(float hum)
{
    s_hum = hum;
}

void sensors_sim_set_lux(float lux)
{
    s_lux = lux;
}

const sensor_driver_t sensors_sim_driver = {
    .init = sensors_sim_init,
    .read_temperature = sensors_sim_read_temperature,
    .read_humidity = sensors_sim_read_humidity,
    .read_lux = sensors_sim_read_lux,
    .deinit = sensors_sim_deinit,
    .get_channel_count = sensors_sim_channel_count,
    .read_temperature_channel = sensors_sim_read_temperature_channel,
    .read_humidity_channel = sensors_sim_read_humidity_channel,
    .read_lux_channel = sensors_sim_read_lux_channel,
};

