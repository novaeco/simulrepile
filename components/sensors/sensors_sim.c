#include "sensors.h"
#include "esp_random.h"
#include <math.h>

static float s_temp = NAN;
static float s_hum = NAN;

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

static void sensors_sim_deinit(void)
{
    s_temp = NAN;
    s_hum = NAN;
}

void sensors_sim_set_temperature(float temp)
{
    s_temp = temp;
}

void sensors_sim_set_humidity(float hum)
{
    s_hum = hum;
}

const sensor_driver_t sensors_sim_driver = {
    .init = sensors_sim_init,
    .read_temperature = sensors_sim_read_temperature,
    .read_humidity = sensors_sim_read_humidity,
    .deinit = sensors_sim_deinit,
};

