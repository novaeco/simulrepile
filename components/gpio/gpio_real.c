#include "gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void gpio_real_mode(uint16_t Pin, uint16_t Mode)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = 1ULL << Pin;

    if (Mode == 0 || Mode == GPIO_MODE_INPUT) {
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    } else if (Mode == GPIO_MODE_INPUT_OUTPUT) {
        io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    } else {
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    }

    gpio_config(&io_conf);
}

static void gpio_real_int(int32_t Pin, gpio_isr_t isr_handler)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pin_bit_mask = 1ULL << Pin;

    gpio_config(&io_conf);

    static bool isr_service_installed = false;
    if (!isr_service_installed) {
        gpio_install_isr_service(0);
        isr_service_installed = true;
    }

    gpio_isr_handler_add(Pin, isr_handler, (void *)Pin);
}

static void gpio_real_write(uint16_t Pin, uint8_t Value)
{
    gpio_set_level(Pin, Value);
}

static uint8_t gpio_real_read(uint16_t Pin)
{
    return gpio_get_level(Pin);
}

static void gpio_real_feed(void)
{
    gpio_set_level(SERVO_FEED_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(SERVO_FEED_PIN, 0);
}

static void gpio_real_water(void)
{
    gpio_set_level(WATER_PUMP_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(WATER_PUMP_PIN, 0);
}

static void gpio_real_heat(void)
{
    gpio_set_level(HEAT_RES_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(5000));
    gpio_set_level(HEAT_RES_PIN, 0);
}

static esp_err_t gpio_real_init(void)
{
    gpio_real_mode(SERVO_FEED_PIN, GPIO_MODE_OUTPUT);
    gpio_real_mode(WATER_PUMP_PIN, GPIO_MODE_OUTPUT);
    gpio_real_mode(HEAT_RES_PIN, GPIO_MODE_OUTPUT);
    gpio_real_write(SERVO_FEED_PIN, 0);
    gpio_real_write(WATER_PUMP_PIN, 0);
    gpio_real_write(HEAT_RES_PIN, 0);
    return ESP_OK;
}

static void gpio_real_deinit(void)
{
    gpio_real_write(WATER_PUMP_PIN, 0);
    gpio_real_write(HEAT_RES_PIN, 0);
    gpio_real_write(SERVO_FEED_PIN, 0);

    gpio_real_mode(WATER_PUMP_PIN, GPIO_MODE_INPUT);
    gpio_real_mode(HEAT_RES_PIN, GPIO_MODE_INPUT);
    gpio_real_mode(SERVO_FEED_PIN, GPIO_MODE_INPUT);
}

const actuator_driver_t gpio_real_driver = {
    .init = gpio_real_init,
    .gpio_mode = gpio_real_mode,
    .gpio_int = gpio_real_int,
    .digital_write = gpio_real_write,
    .digital_read = gpio_real_read,
    .feed = gpio_real_feed,
    .water = gpio_real_water,
    .heat = gpio_real_heat,
    .deinit = gpio_real_deinit,
};

