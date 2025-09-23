#include "gpio.h"
#include "game_mode.h"
#include "esp_log.h"

extern const actuator_driver_t gpio_real_driver;
extern const actuator_driver_t gpio_sim_driver;

static const char *TAG = "gpio";
static const actuator_driver_t *s_driver = NULL;

static void gpio_select_driver(void)
{
    if (!s_driver) {
        s_driver = (game_mode_get() == GAME_MODE_SIMULATION) ?
                       &gpio_sim_driver : &gpio_real_driver;
    }
}

esp_err_t reptile_actuators_init(void)
{
    gpio_select_driver();
    if (s_driver && s_driver->init) {
        return s_driver->init();
    }
    return ESP_OK;
}

void DEV_GPIO_Mode(gpio_num_t Pin, gpio_mode_t Mode)
{
    gpio_select_driver();
    if (s_driver && s_driver->gpio_mode) {
        s_driver->gpio_mode(Pin, Mode);
    }
}

void DEV_GPIO_INT(gpio_num_t Pin, gpio_isr_t isr_handler)
{
    gpio_select_driver();
    if (s_driver && s_driver->gpio_int) {
        s_driver->gpio_int(Pin, isr_handler);
    }
}

void DEV_Digital_Write(gpio_num_t Pin, uint8_t Value)
{
    gpio_select_driver();
    if (s_driver && s_driver->digital_write) {
        s_driver->digital_write(Pin, Value);
    }
}

uint8_t DEV_Digital_Read(gpio_num_t Pin)
{
    gpio_select_driver();
    if (s_driver && s_driver->digital_read) {
        return s_driver->digital_read(Pin);
    }
    return 0;
}

static bool channel_supported(size_t channel)
{
    gpio_select_driver();
    if (!s_driver) {
        return false;
    }
    size_t max_channels = s_driver->channel_count;
    if (max_channels == 0 || channel < max_channels) {
        return true;
    }
    ESP_LOGW(TAG,
             "Actuator channel %zu ignored (driver supports %zu channel%s)",
             channel,
             max_channels,
             max_channels > 1 ? "s" : "");
    return false;
}

void reptile_feed_gpio_channel(size_t channel)
{
    if (!channel_supported(channel)) {
        return;
    }
    if (s_driver->feed) {
        s_driver->feed(channel);
    }
}

void reptile_water_gpio_channel(size_t channel)
{
    if (!channel_supported(channel)) {
        return;
    }
    if (s_driver->water) {
        s_driver->water(channel);
    }
}

void reptile_heat_gpio_channel(size_t channel)
{
    if (!channel_supported(channel)) {
        return;
    }
    if (s_driver->heat) {
        s_driver->heat(channel);
    }
}

void reptile_uv_gpio_channel(size_t channel, bool on)
{
    if (!channel_supported(channel)) {
        return;
    }
    if (s_driver->uv) {
        s_driver->uv(channel, on);
    }
}

void reptile_feed_gpio(void)
{
    reptile_feed_gpio_channel(0);
}

void reptile_water_gpio(void)
{
    reptile_water_gpio_channel(0);
}

void reptile_heat_gpio(void)
{
    reptile_heat_gpio_channel(0);
}

void reptile_uv_gpio(bool on)
{
    reptile_uv_gpio_channel(0, on);
}

size_t reptile_actuator_channel_count(void)
{
    gpio_select_driver();
    return s_driver ? s_driver->channel_count : 0u;
}

void reptile_actuators_deinit(void)
{
    if (s_driver && s_driver->deinit) {
        s_driver->deinit();
    }
    s_driver = NULL;
}

