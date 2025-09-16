#include "sensors.h"
#include "esp_random.h"
#include <math.h>

static float s_temp = NAN;
static float s_hum = NAN;
static float s_lux = NAN;
static sensor_uv_data_t s_uv = {
    .uva = NAN,
    .uvb = NAN,
    .uv_index = NAN,
};

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
    return 300.0f + (float)(randv % 700);
}

static sensor_uv_data_t sensors_sim_read_uv(void)
{
    if (!isnan(s_uv.uv_index)) {
        return s_uv;
    }
    uint32_t randv = esp_random();
    float index = 2.0f + (float)(randv % 30) / 10.0f; // 2.0 - 4.9
    sensor_uv_data_t data = {
        .uv_index = index,
        .uva = index * 0.6f,
        .uvb = index * 0.4f,
    };
    return data;
}

static void sensors_sim_deinit(void)
{
    s_temp = NAN;
    s_hum = NAN;
    s_lux = NAN;
    s_uv.uva = NAN;
    s_uv.uvb = NAN;
    s_uv.uv_index = NAN;
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

void sensors_sim_set_uv(float uva, float uvb)
{
    s_uv.uva = uva;
    s_uv.uvb = uvb;
    if (isnan(uva) || isnan(uvb)) {
        s_uv.uv_index = NAN;
    } else {
        s_uv.uv_index = (uva + uvb) * 0.5f;
    }
}

const sensor_driver_t sensors_sim_driver = {
    .init = sensors_sim_init,
    .read_temperature = sensors_sim_read_temperature,
    .read_humidity = sensors_sim_read_humidity,
    .read_lux = sensors_sim_read_lux,
    .read_uv = sensors_sim_read_uv,
    .deinit = sensors_sim_deinit,
};

