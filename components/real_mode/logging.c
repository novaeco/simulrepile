/* Logging of sensor data to storage */

#include "logging.h"
#include "storage/logs.h"
#include "storage.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* Nominal power ratings for each actuator (watts) */
#define HEATER_POWER_W       50.0f
#define UV_POWER_W           10.0f
#define NEON_POWER_W         15.0f
#define PUMP_POWER_W          8.0f
#define FAN_POWER_W           5.0f
#define HUMIDIFIER_POWER_W   12.0f

#define MAX_TERRARIUMS 8

#define ACTUATOR_BIT_HEATER      (1U << 0)
#define ACTUATOR_BIT_UV          (1U << 1)
#define ACTUATOR_BIT_NEON        (1U << 2)
#define ACTUATOR_BIT_PUMP        (1U << 3)
#define ACTUATOR_BIT_FAN         (1U << 4)
#define ACTUATOR_BIT_HUMIDIFIER  (1U << 5)

static const char *TAG = "logging";
static time_t s_last_ts[MAX_TERRARIUMS];
static bool s_initialized;

esp_err_t logging_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (!storage_init()) {
        struct stat st;
        if (stat("/sdcard", &st) != 0 || !S_ISDIR(st.st_mode)) {
            ESP_LOGE(TAG, "storage_init failed");
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "Réutilisation d'un montage SD existant");
    }
    for (size_t i = 0; i < MAX_TERRARIUMS; ++i) {
        s_last_ts[i] = 0;
    }
    s_initialized = true;
    return ESP_OK;
}

static float compute_consumption_wh(const terrarium_hw_t *hw,
                                    size_t idx,
                                    time_t now,
                                    uint32_t *mask_out)
{
    float power_w = 0.0f;
    uint32_t mask = 0;

    if (GPIO_IS_VALID_GPIO(hw->heater_gpio) && gpio_get_level(hw->heater_gpio)) {
        power_w += HEATER_POWER_W;
        mask |= ACTUATOR_BIT_HEATER;
    }
    if (GPIO_IS_VALID_GPIO(hw->uv_gpio) && gpio_get_level(hw->uv_gpio)) {
        power_w += UV_POWER_W;
        mask |= ACTUATOR_BIT_UV;
    }
    if (GPIO_IS_VALID_GPIO(hw->neon_gpio) && gpio_get_level(hw->neon_gpio)) {
        power_w += NEON_POWER_W;
        mask |= ACTUATOR_BIT_NEON;
    }
    if (GPIO_IS_VALID_GPIO(hw->pump_gpio) && gpio_get_level(hw->pump_gpio)) {
        power_w += PUMP_POWER_W;
        mask |= ACTUATOR_BIT_PUMP;
    }
    if (GPIO_IS_VALID_GPIO(hw->fan_gpio) && gpio_get_level(hw->fan_gpio)) {
        power_w += FAN_POWER_W;
        mask |= ACTUATOR_BIT_FAN;
    }
    if (GPIO_IS_VALID_GPIO(hw->humidifier_gpio) && gpio_get_level(hw->humidifier_gpio)) {
        power_w += HUMIDIFIER_POWER_W;
        mask |= ACTUATOR_BIT_HUMIDIFIER;
    }

    if (mask_out) {
        *mask_out = mask;
    }

    time_t prev = s_last_ts[idx];
    s_last_ts[idx] = now;
    if (prev == 0 || now <= prev) {
        return 0.0f;
    }
    float dt_h = (float)(now - prev) / 3600.0f;
    return power_w * dt_h;
}

esp_err_t logging_write(size_t terrarium_idx,
                        const terrarium_hw_t *hw,
                        sensor_data_t *data)
{
    if (!hw || !data || terrarium_idx >= MAX_TERRARIUMS) {
        return ESP_ERR_INVALID_ARG;
    }

    time_t now = time(NULL);
    uint32_t state_mask = 0;
    data->power_w = compute_consumption_wh(hw, terrarium_idx, now, &state_mask);

    storage_log_entry_t entry = {
        .timestamp = now,
        .temperature = data->temperature_c,
        .humidity = data->humidity_pct,
        .uv_index = data->luminosity_lux,
        .co2 = data->co2_ppm,
        .actuator_mask = state_mask,
        .power = data->power_w,
    };

    char terrarium[32];
    snprintf(terrarium, sizeof(terrarium), "terrarium_%zu", terrarium_idx);
    if (!storage_append_log(terrarium, &entry, STORAGE_LOG_CSV)) {
        ESP_LOGE(TAG, "append log failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}
