#include "actuators.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "actuators";

#define ACTUATOR_COUNT 6
enum actuator_index {
    ACT_HEATER = 0,
    ACT_UV,
    ACT_NEON,
    ACT_PUMP,
    ACT_FAN,
    ACT_HUMIDIFIER
};

#define ANTI_FLASH_DELAY_US (500 * 1000)      /* 500 ms */
#define EMERGENCY_TEMP_C    40.0f
#define WATCHDOG_TIMEOUT_US (10 * 1000 * 1000) /* 10 s */

typedef struct {
    const terrarium_hw_t *hw;
    esp_timer_handle_t watchdog;
    int current_level[ACTUATOR_COUNT];
    int64_t last_change_us[ACTUATOR_COUNT];
} actuator_ctx_t;

/* Support up to 8 terrariums */
#define MAX_TERRARIUMS 8
static actuator_ctx_t g_ctxs[MAX_TERRARIUMS];
static size_t g_ctx_count = 0;

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

static void disable_all(actuator_ctx_t *ctx)
{
    const terrarium_hw_t *hw = ctx->hw;
    int64_t now = esp_timer_get_time();
    gpio_safe_set(hw->heater_gpio, 0);
    gpio_safe_set(hw->uv_gpio, 0);
    gpio_safe_set(hw->neon_gpio, 0);
    gpio_safe_set(hw->pump_gpio, 0);
    gpio_safe_set(hw->fan_gpio, 0);
    gpio_safe_set(hw->humidifier_gpio, 0);
    for (int i = 0; i < ACTUATOR_COUNT; ++i) {
        ctx->current_level[i] = 0;
        ctx->last_change_us[i] = now;
    }
}

static void watchdog_cb(void *arg)
{
    actuator_ctx_t *ctx = (actuator_ctx_t *)arg;
    ESP_LOGW(TAG, "Sensor watchdog timeout, disabling actuators");
    disable_all(ctx);
}

static actuator_ctx_t *find_ctx(const terrarium_hw_t *hw)
{
    for (size_t i = 0; i < g_ctx_count; ++i) {
        if (g_ctxs[i].hw == hw) {
            return &g_ctxs[i];
        }
    }
    return NULL;
}

static void set_actuator(actuator_ctx_t *ctx, enum actuator_index idx,
                         gpio_num_t gpio, int level)
{
    int64_t now = esp_timer_get_time();
    if (ctx->current_level[idx] != level &&
        now - ctx->last_change_us[idx] < ANTI_FLASH_DELAY_US) {
        return; /* ignore rapid toggling */
    }

    ctx->current_level[idx] = level;
    ctx->last_change_us[idx] = now;
    gpio_safe_set(gpio, level);
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
        return ret;
    }

    if (g_ctx_count >= sizeof(g_ctxs)/sizeof(g_ctxs[0])) {
        ESP_LOGE(TAG, "too many terrariums for actuator context");
        return ESP_ERR_NO_MEM;
    }

    actuator_ctx_t *ctx = &g_ctxs[g_ctx_count++];
    ctx->hw = hw;
    for (int i = 0; i < ACTUATOR_COUNT; ++i) {
        ctx->current_level[i] = 0;
        ctx->last_change_us[i] = 0;
    }

    esp_timer_create_args_t targs = {
        .callback = watchdog_cb,
        .arg = ctx,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "act_wd"
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &ctx->watchdog));
    ESP_ERROR_CHECK(esp_timer_start_once(ctx->watchdog, WATCHDOG_TIMEOUT_US));

    return ESP_OK;
}

static void feed_watchdog(actuator_ctx_t *ctx)
{
    esp_timer_stop(ctx->watchdog);
    esp_timer_start_once(ctx->watchdog, WATCHDOG_TIMEOUT_US);
}

void actuators_watchdog_feed(const terrarium_hw_t *hw)
{
    actuator_ctx_t *ctx = find_ctx(hw);
    if (ctx) {
        feed_watchdog(ctx);
    }
}

esp_err_t actuators_apply(const terrarium_hw_t *hw, const sensor_data_t *data,
                          const real_mode_state_t *state)
{
    actuator_ctx_t *ctx = find_ctx(hw);
    if (!ctx) {
        return ESP_ERR_INVALID_STATE;
    }

    if (state && state->manual_mode) {
        set_actuator(ctx, ACT_HEATER, hw->heater_gpio, state->actuators.heater);
        set_actuator(ctx, ACT_UV, hw->uv_gpio, state->actuators.uv);
        set_actuator(ctx, ACT_NEON, hw->neon_gpio, state->actuators.neon);
        set_actuator(ctx, ACT_PUMP, hw->pump_gpio, state->actuators.pump);
        set_actuator(ctx, ACT_FAN, hw->fan_gpio, state->actuators.fan);
        set_actuator(ctx, ACT_HUMIDIFIER, hw->humidifier_gpio, state->actuators.humidifier);
        return ESP_OK;
    }

    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    if (data->temperature_c > EMERGENCY_TEMP_C) {
        ESP_LOGE(TAG, "Emergency cut-off: temperature %.2f > %.2f", data->temperature_c, EMERGENCY_TEMP_C);
        disable_all(ctx);
        return ESP_OK;
    }

    if (data->temperature_c < hw->temp_low_c) {
        set_actuator(ctx, ACT_HEATER, hw->heater_gpio, 1);
    } else if (data->temperature_c > hw->temp_high_c) {
        set_actuator(ctx, ACT_HEATER, hw->heater_gpio, 0);
    }

    if (data->humidity_pct < hw->humidity_low_pct) {
        set_actuator(ctx, ACT_PUMP, hw->pump_gpio, 1);
        set_actuator(ctx, ACT_HUMIDIFIER, hw->humidifier_gpio, 1);
    } else if (data->humidity_pct > hw->humidity_high_pct) {
        set_actuator(ctx, ACT_PUMP, hw->pump_gpio, 0);
        set_actuator(ctx, ACT_HUMIDIFIER, hw->humidifier_gpio, 0);
    }

    if (data->luminosity_lux < hw->lux_low_lx) {
        set_actuator(ctx, ACT_UV, hw->uv_gpio, 1);
        set_actuator(ctx, ACT_NEON, hw->neon_gpio, 1);
    } else if (data->luminosity_lux > hw->lux_high_lx) {
        set_actuator(ctx, ACT_UV, hw->uv_gpio, 0);
        set_actuator(ctx, ACT_NEON, hw->neon_gpio, 0);
    }

    if (data->co2_ppm > hw->co2_high_ppm) {
        set_actuator(ctx, ACT_FAN, hw->fan_gpio, 1);
    } else {
        set_actuator(ctx, ACT_FAN, hw->fan_gpio, 0);
    }

    return ESP_OK;
}

