#include "gpio.h"
#include "game_mode.h"
#include "esp_log.h"

extern const actuator_driver_t gpio_real_driver;
extern const actuator_driver_t gpio_sim_driver;

static const char *TAG = "gpio";
static const actuator_driver_t *s_driver = NULL;

static inline void ensure_driver(void)
{
    game_mode_t mode = game_mode_get();
    const actuator_driver_t *expected =
        (mode == GAME_MODE_SIMULATION) ? &gpio_sim_driver : &gpio_real_driver;

    if (s_driver != expected) {
        s_driver = expected;
    }
}

esp_err_t reptile_actuators_init(void)
{
    ensure_driver();
    if (s_driver && s_driver->init) {
        return s_driver->init();
    }
    return ESP_OK;
}

void DEV_GPIO_Mode(uint16_t Pin, uint16_t Mode)
{
    ensure_driver();
    if (s_driver && s_driver->gpio_mode) {
        s_driver->gpio_mode(Pin, Mode);
    }
}

void DEV_GPIO_INT(int32_t Pin, gpio_isr_t isr_handler)
{
    ensure_driver();
    if (s_driver && s_driver->gpio_int) {
        s_driver->gpio_int(Pin, isr_handler);
    }
}

void DEV_Digital_Write(uint16_t Pin, uint8_t Value)
{
    ensure_driver();
    if (s_driver && s_driver->digital_write) {
        s_driver->digital_write(Pin, Value);
    }
}

uint8_t DEV_Digital_Read(uint16_t Pin)
{
    ensure_driver();
    if (s_driver && s_driver->digital_read) {
        return s_driver->digital_read(Pin);
    }
    return 0;
}

static bool channel_supported(size_t channel)
{
    ensure_driver();
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
    ensure_driver();
    return s_driver ? s_driver->channel_count : 0u;
}

void reptile_actuators_deinit(void)
{
    if (s_driver && s_driver->deinit) {
        s_driver->deinit();
    }
    s_driver = NULL;
}

