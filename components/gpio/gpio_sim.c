#include "gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdbool.h>
#include <string.h>

static const char *TAG = "gpio_sim";

#ifndef CONFIG_SOC_GPIO_PIN_COUNT
#define CONFIG_SOC_GPIO_PIN_COUNT 49
#endif

#define SIM_GPIO_LEVEL_COUNT CONFIG_SOC_GPIO_PIN_COUNT

static uint8_t s_levels[SIM_GPIO_LEVEL_COUNT];

#define SIM_MAX_CHANNELS 4

static bool s_heater_state[SIM_MAX_CHANNELS];
static bool s_pump_state[SIM_MAX_CHANNELS];
static bool s_uv_state[SIM_MAX_CHANNELS];

static bool sim_gpio_index(gpio_num_t pin, size_t *index)
{
    if (pin >= 0 && pin < SIM_GPIO_LEVEL_COUNT) {
        if (index) {
            *index = (size_t)pin;
        }
        return true;
    }

    return false;
}

static bool sim_channel_valid(size_t channel)
{
    if (channel < SIM_MAX_CHANNELS) {
        return true;
    }
    ESP_LOGW(TAG, "Simulated actuator channel %zu exceeds %u", channel, SIM_MAX_CHANNELS);
    return false;
}

static void gpio_sim_mode(gpio_num_t Pin, gpio_mode_t Mode)
{
    (void)Pin;
    (void)Mode;
}

static void gpio_sim_int(gpio_num_t Pin, gpio_isr_t isr_handler)
{
    (void)Pin;
    (void)isr_handler;
}

static void gpio_sim_write(gpio_num_t Pin, uint8_t Value)
{
    size_t index = 0;
    if (!sim_gpio_index(Pin, &index)) {
        ESP_LOGV(TAG, "GPIO virtuel %d hors plage ignoré", Pin);
        return;
    }

    const uint8_t stored_level = Value ? 1u : 0u;
    s_levels[index] = stored_level;

    if (Pin == LED_GPIO_PIN) {
        s_uv_state[0] = (stored_level != 0);
    }
}

static uint8_t gpio_sim_read(gpio_num_t Pin)
{
    size_t index = 0;
    if (!sim_gpio_index(Pin, &index)) {
        return 0u;
    }

    return s_levels[index];
}

bool gpio_sim_get_heater_state(void)
{
    return s_heater_state[0];
}

bool gpio_sim_get_pump_state(void)
{
    return s_pump_state[0];
}

bool gpio_sim_get_uv_state(void)
{
    return s_uv_state[0];
}

static void gpio_sim_feed(size_t channel)
{
    if (!sim_channel_valid(channel)) {
        return;
    }
    if (channel != 0) {
        ESP_LOGW(TAG, "Feed actuator not modelled for channel %zu", channel);
        return;
    }
    ESP_LOGI(TAG, "Simulated feed on channel %zu", channel);
}

static void gpio_sim_water(size_t channel)
{
    if (!sim_channel_valid(channel)) {
        return;
    }
    ESP_LOGI(TAG, "Simulated water on channel %zu", channel);
    s_pump_state[channel] = true;
    vTaskDelay(pdMS_TO_TICKS(REPTILE_GPIO_PUMP_PULSE_MS));
    s_pump_state[channel] = false;
}

static void gpio_sim_heat(size_t channel)
{
    if (!sim_channel_valid(channel)) {
        return;
    }
    ESP_LOGI(TAG, "Simulated heat on channel %zu", channel);
    s_heater_state[channel] = true;
    vTaskDelay(pdMS_TO_TICKS(REPTILE_GPIO_HEAT_PULSE_MS));
    s_heater_state[channel] = false;
}

static void gpio_sim_uv(size_t channel, bool on)
{
    if (!sim_channel_valid(channel)) {
        return;
    }
    ESP_LOGI(TAG, "Simulated UV %s on channel %zu", on ? "ON" : "OFF", channel);
    s_uv_state[channel] = on;
}

static void gpio_sim_deinit(void)
{
    memset(s_levels, 0, sizeof(s_levels));
    memset(s_heater_state, 0, sizeof(s_heater_state));
    memset(s_pump_state, 0, sizeof(s_pump_state));
    memset(s_uv_state, 0, sizeof(s_uv_state));
}

static esp_err_t gpio_sim_init(void)
{
    memset(s_levels, 0, sizeof(s_levels));
    memset(s_heater_state, 0, sizeof(s_heater_state));
    memset(s_pump_state, 0, sizeof(s_pump_state));
    memset(s_uv_state, 0, sizeof(s_uv_state));
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
    .uv = gpio_sim_uv,
    .deinit = gpio_sim_deinit,
    .channel_count = SIM_MAX_CHANNELS,
};

