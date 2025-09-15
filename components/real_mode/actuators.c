#include "actuators.h"
#include "driver/gpio.h"
#include "esp_log.h"

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

esp_err_t actuators_apply(const terrarium_hw_t *hw, const sensor_data_t *data,
                          const real_mode_state_t *state)
{
    if (state && state->manual_mode) {
        gpio_safe_set(hw->heater_gpio, state->actuators.heater);
        gpio_safe_set(hw->uv_gpio, state->actuators.uv);
        gpio_safe_set(hw->neon_gpio, state->actuators.neon);
        gpio_safe_set(hw->pump_gpio, state->actuators.pump);
        gpio_safe_set(hw->fan_gpio, state->actuators.fan);
        gpio_safe_set(hw->humidifier_gpio, state->actuators.humidifier);
        return ESP_OK;
    }

    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    if (data->temperature_c < hw->temp_low_c) {
        gpio_safe_set(hw->heater_gpio, 1);
    } else if (data->temperature_c > hw->temp_high_c) {
        gpio_safe_set(hw->heater_gpio, 0);
    }

    if (data->humidity_pct < hw->humidity_low_pct) {
        gpio_safe_set(hw->pump_gpio, 1);
        gpio_safe_set(hw->humidifier_gpio, 1);
    } else if (data->humidity_pct > hw->humidity_high_pct) {
        gpio_safe_set(hw->pump_gpio, 0);
        gpio_safe_set(hw->humidifier_gpio, 0);
    }

    if (data->luminosity_lux < hw->lux_low_lx) {
        gpio_safe_set(hw->uv_gpio, 1);
        gpio_safe_set(hw->neon_gpio, 1);
    } else if (data->luminosity_lux > hw->lux_high_lx) {
        gpio_safe_set(hw->uv_gpio, 0);
        gpio_safe_set(hw->neon_gpio, 0);
    }

    if (data->co2_ppm > hw->co2_high_ppm) {
        gpio_safe_set(hw->fan_gpio, 1);
    } else {
        gpio_safe_set(hw->fan_gpio, 0);
    }

    return ESP_OK;
}
