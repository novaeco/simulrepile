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

void reptile_fan_set(bool on)
{
    gpio_select_driver();
    if (s_driver && s_driver->set_fan) {
        s_driver->set_fan(on);
    }
}

bool reptile_fan_get(void)
{
    gpio_select_driver();
    if (s_driver && s_driver->get_fan) {
        return s_driver->get_fan();
    }
    return false;
}

void reptile_uv_lamp_set(bool on)
{
    gpio_select_driver();
    if (s_driver && s_driver->set_uv_lamp) {
        s_driver->set_uv_lamp(on);
    }
}

bool reptile_uv_lamp_get(void)
{
    gpio_select_driver();
    if (s_driver && s_driver->get_uv_lamp) {
        return s_driver->get_uv_lamp();
    }
    return false;
}

void reptile_light_set(bool on)
{
    gpio_select_driver();
    if (s_driver && s_driver->set_light) {
        s_driver->set_light(on);
    }
}

bool reptile_light_get(void)
{
    gpio_select_driver();
    if (s_driver && s_driver->get_light) {
        return s_driver->get_light();
    }
    return false;
}

void reptile_actuators_deinit(void)
{
    if (s_driver && s_driver->deinit) {
        s_driver->deinit();
    }
    s_driver = NULL;
}

