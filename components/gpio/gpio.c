#include "gpio.h"
#include "game_mode.h"

extern const actuator_driver_t gpio_real_driver;
extern const actuator_driver_t gpio_sim_driver;

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

void DEV_GPIO_Mode(uint16_t Pin, uint16_t Mode)
{
    gpio_select_driver();
    if (s_driver && s_driver->gpio_mode) {
        s_driver->gpio_mode(Pin, Mode);
    }
}

void DEV_GPIO_INT(int32_t Pin, gpio_isr_t isr_handler)
{
    gpio_select_driver();
    if (s_driver && s_driver->gpio_int) {
        s_driver->gpio_int(Pin, isr_handler);
    }
}

void DEV_Digital_Write(uint16_t Pin, uint8_t Value)
{
    gpio_select_driver();
    if (s_driver && s_driver->digital_write) {
        s_driver->digital_write(Pin, Value);
    }
}

uint8_t DEV_Digital_Read(uint16_t Pin)
{
    gpio_select_driver();
    if (s_driver && s_driver->digital_read) {
        return s_driver->digital_read(Pin);
    }
    return 0;
}

void reptile_feed_gpio(void)
{
    gpio_select_driver();
    if (s_driver && s_driver->feed) {
        s_driver->feed();
    }
}

void reptile_water_gpio(void)
{
    gpio_select_driver();
    if (s_driver && s_driver->water) {
        s_driver->water();
    }
}

void reptile_heat_gpio(void)
{
    gpio_select_driver();
    if (s_driver && s_driver->heat) {
        s_driver->heat();
    }
}

void reptile_actuators_deinit(void)
{
    if (s_driver && s_driver->deinit) {
        s_driver->deinit();
    }
    s_driver = NULL;
}

