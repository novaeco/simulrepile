#include "actuators.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TEMP_HIGH_C 35.0f
#define TEMP_LOW_C 20.0f

static const char *TAG = "actuators";

static inline esp_err_t gpio_safe_set(gpio_num_t gpio, int level)
{
    if (gpio == GPIO_NUM_NC) {
        return ESP_OK;
    }
    esp_err_t ret = gpio_set_level(gpio, level);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d set failed: %s", gpio, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t actuators_init(const terrarium_hw_t *hw)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL<<hw->heater_gpio) | (1ULL<<hw->uv_gpio) |
                        (1ULL<<hw->neon_gpio) | (1ULL<<hw->pump_gpio) |
                        (1ULL<<hw->fan_gpio) | (1ULL<<hw->humidifier_gpio),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t actuators_apply(const terrarium_hw_t *hw, const sensor_data_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    if (data->temperature_c < TEMP_LOW_C) {
        gpio_safe_set(hw->heater_gpio, 1);
    } else if (data->temperature_c > TEMP_HIGH_C) {
        gpio_safe_set(hw->heater_gpio, 0);
    }

    /* Exemple : activer ventilation si CO2 trop élevé */
    if (data->co2_ppm > 1500.0f) {
        gpio_safe_set(hw->fan_gpio, 1);
    } else {
        gpio_safe_set(hw->fan_gpio, 0);
    }

    return ESP_OK;
}
