#include "gpio.h"
#include "esp_log.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "gpio_sim";
static uint8_t s_levels[256];
static bool s_heater_state;
static bool s_pump_state;

static void gpio_sim_mode(uint16_t Pin, uint16_t Mode)
{
    (void)Pin;
    (void)Mode;
}

static void gpio_sim_int(int32_t Pin, gpio_isr_t isr_handler)
{
    (void)Pin;
    (void)isr_handler;
}

static void gpio_sim_write(uint16_t Pin, uint8_t Value)
{
    s_levels[Pin & 0xFF] = Value;
    if (Pin == HEAT_RES_PIN) {
        s_heater_state = Value;
    }
    if (Pin == WATER_PUMP_PIN) {
        s_pump_state = Value;
    }
}

static uint8_t gpio_sim_read(uint16_t Pin)
{
    return s_levels[Pin & 0xFF];
}

bool gpio_sim_get_heater_state(void)
{
    return s_heater_state;
}

bool gpio_sim_get_pump_state(void)
{
    return s_pump_state;
}

static void gpio_sim_feed(void)
{
    ESP_LOGI(TAG, "Simulated feed");
}

static void gpio_sim_water(void)
{
    ESP_LOGI(TAG, "Simulated water");
}

static void gpio_sim_heat(void)
{
    ESP_LOGI(TAG, "Simulated heat");
}

static void gpio_sim_deinit(void)
{
    memset(s_levels, 0, sizeof(s_levels));
    s_heater_state = false;
    s_pump_state = false;
}

static esp_err_t gpio_sim_init(void)
{
    memset(s_levels, 0, sizeof(s_levels));
    s_heater_state = false;
    s_pump_state = false;
    return ESP_OK;
}

const actuator_driver_t gpio_sim_driver = {
    .init = gpio_sim_init,
    .gpio_mode = gpio_sim_mode,
    .gpio_int = gpio_sim_int,
    .digital_write = gpio_sim_write,
    .digital_read = gpio_sim_read,
    .feed = gpio_sim_feed,
    .water = gpio_sim_water,
    .heat = gpio_sim_heat,
    .deinit = gpio_sim_deinit,
};

