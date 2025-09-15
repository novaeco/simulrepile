#include "actuators.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

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

extern terrarium_hw_t g_terrariums[];
extern const size_t g_terrarium_count;
extern terrarium_device_status_t g_device_status[];

typedef struct {
    const terrarium_hw_t *hw;
    esp_timer_handle_t watchdog;
    int current_level[ACTUATOR_COUNT];
    int64_t last_change_us[ACTUATOR_COUNT];
    int terrarium_index;
} actuator_ctx_t;

/* Support up to 8 terrariums */
#define MAX_TERRARIUMS 8
static actuator_ctx_t g_ctxs[MAX_TERRARIUMS];
static size_t g_ctx_count = 0;

static int find_hw_index(const terrarium_hw_t *hw)
{
    for (size_t i = 0; i < g_terrarium_count; ++i) {
        if (&g_terrariums[i] == hw) {
            return (int)i;
        }
    }
    return -1;
}

static inline esp_err_t gpio_safe_set(gpio_num_t gpio, int level)
{
    if (gpio == GPIO_NUM_NC || !GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
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
                         gpio_num_t gpio, int level, bool available)
{
    int64_t now = esp_timer_get_time();

    if (!available) {
        ctx->current_level[idx] = 0;
        ctx->last_change_us[idx] = now;
        gpio_safe_set(gpio, 0);
        return;
    }

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
    uint64_t mask = 0;
    if (GPIO_IS_VALID_OUTPUT_GPIO(hw->heater_gpio))      mask |= (1ULL << hw->heater_gpio);
    if (GPIO_IS_VALID_OUTPUT_GPIO(hw->uv_gpio))          mask |= (1ULL << hw->uv_gpio);
    if (GPIO_IS_VALID_OUTPUT_GPIO(hw->neon_gpio))        mask |= (1ULL << hw->neon_gpio);
    if (GPIO_IS_VALID_OUTPUT_GPIO(hw->pump_gpio))        mask |= (1ULL << hw->pump_gpio);
    if (GPIO_IS_VALID_OUTPUT_GPIO(hw->fan_gpio))         mask |= (1ULL << hw->fan_gpio);
    if (GPIO_IS_VALID_OUTPUT_GPIO(hw->humidifier_gpio))  mask |= (1ULL << hw->humidifier_gpio);

    if (mask != 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = mask,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    if (g_ctx_count >= sizeof(g_ctxs)/sizeof(g_ctxs[0])) {
        ESP_LOGE(TAG, "too many terrariums for actuator context");
        return ESP_ERR_NO_MEM;
    }

    actuator_ctx_t *ctx = &g_ctxs[g_ctx_count++];
    ctx->hw = hw;
    ctx->terrarium_index = find_hw_index(hw);
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

static bool detect_gpio_output(gpio_num_t gpio)
{
    if (gpio == GPIO_NUM_NC || !GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
        return false;
    }
    int current = gpio_get_level(gpio);
    esp_err_t ret = gpio_set_level(gpio, current);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "GPIO %d handshake failed: %s", gpio, esp_err_to_name(ret));
        return false;
    }
    return true;
}

actuator_connection_t actuators_detect(const terrarium_hw_t *hw)
{
    actuator_connection_t status = {
        .heater = detect_gpio_output(hw->heater_gpio),
        .uv = detect_gpio_output(hw->uv_gpio),
        .neon = detect_gpio_output(hw->neon_gpio),
        .pump = detect_gpio_output(hw->pump_gpio),
        .fan = detect_gpio_output(hw->fan_gpio),
        .humidifier = detect_gpio_output(hw->humidifier_gpio),
    };

    int idx = find_hw_index(hw);
    if (idx >= 0) {
        ESP_LOGI(TAG, "Terrarium %d actuators heater:%s uv:%s neon:%s pump:%s fan:%s humidifier:%s",
                 idx,
                 status.heater ? "OK" : "absent",
                 status.uv ? "OK" : "absent",
                 status.neon ? "OK" : "absent",
                 status.pump ? "OK" : "absent",
                 status.fan ? "OK" : "absent",
                 status.humidifier ? "OK" : "absent");
    }

    return status;
}

esp_err_t actuators_apply(const terrarium_hw_t *hw, const sensor_data_t *data,
                          const real_mode_state_t *state)
{
    actuator_ctx_t *ctx = find_ctx(hw);
    if (!ctx) {
        return ESP_ERR_INVALID_STATE;
    }

    const actuator_connection_t *act_conn = NULL;
    const sensor_connection_t *sensor_conn = NULL;
    if (ctx->terrarium_index >= 0 && (size_t)ctx->terrarium_index < g_terrarium_count) {
        act_conn = &g_device_status[ctx->terrarium_index].actuators;
        sensor_conn = &g_device_status[ctx->terrarium_index].sensors;
    }

    if (state && state->manual_mode) {
        set_actuator(ctx, ACT_HEATER, hw->heater_gpio, state->actuators.heater, act_conn && act_conn->heater);
        set_actuator(ctx, ACT_UV, hw->uv_gpio, state->actuators.uv, act_conn && act_conn->uv);
        set_actuator(ctx, ACT_NEON, hw->neon_gpio, state->actuators.neon, act_conn && act_conn->neon);
        set_actuator(ctx, ACT_PUMP, hw->pump_gpio, state->actuators.pump, act_conn && act_conn->pump);
        set_actuator(ctx, ACT_FAN, hw->fan_gpio, state->actuators.fan, act_conn && act_conn->fan);
        set_actuator(ctx, ACT_HUMIDIFIER, hw->humidifier_gpio, state->actuators.humidifier, act_conn && act_conn->humidifier);
        return ESP_OK;
    }

    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    if (sensor_conn && sensor_conn->temperature_humidity && data->temperature_c > EMERGENCY_TEMP_C) {
        ESP_LOGE(TAG, "Emergency cut-off: temperature %.2f > %.2f", data->temperature_c, EMERGENCY_TEMP_C);
        disable_all(ctx);
        return ESP_OK;
    }

    bool have_temp = sensor_conn && sensor_conn->temperature_humidity && !isnan(data->temperature_c);
    bool have_humidity = sensor_conn && sensor_conn->temperature_humidity && !isnan(data->humidity_pct);
    bool have_lux = sensor_conn && sensor_conn->luminosity && !isnan(data->luminosity_lux);
    bool have_co2 = sensor_conn && sensor_conn->co2 && !isnan(data->co2_ppm);

    if (!have_temp) {
        set_actuator(ctx, ACT_HEATER, hw->heater_gpio, 0, act_conn && act_conn->heater);
    } else if (data->temperature_c < hw->temp_low_c) {
        set_actuator(ctx, ACT_HEATER, hw->heater_gpio, 1, act_conn && act_conn->heater);
    } else if (data->temperature_c > hw->temp_high_c) {
        set_actuator(ctx, ACT_HEATER, hw->heater_gpio, 0, act_conn && act_conn->heater);
    }

    if (!have_humidity) {
        set_actuator(ctx, ACT_PUMP, hw->pump_gpio, 0, act_conn && act_conn->pump);
        set_actuator(ctx, ACT_HUMIDIFIER, hw->humidifier_gpio, 0, act_conn && act_conn->humidifier);
    } else if (data->humidity_pct < hw->humidity_low_pct) {
        set_actuator(ctx, ACT_PUMP, hw->pump_gpio, 1, act_conn && act_conn->pump);
        set_actuator(ctx, ACT_HUMIDIFIER, hw->humidifier_gpio, 1, act_conn && act_conn->humidifier);
    } else if (data->humidity_pct > hw->humidity_high_pct) {
        set_actuator(ctx, ACT_PUMP, hw->pump_gpio, 0, act_conn && act_conn->pump);
        set_actuator(ctx, ACT_HUMIDIFIER, hw->humidifier_gpio, 0, act_conn && act_conn->humidifier);
    }

    if (!have_lux) {
        set_actuator(ctx, ACT_UV, hw->uv_gpio, 0, act_conn && act_conn->uv);
        set_actuator(ctx, ACT_NEON, hw->neon_gpio, 0, act_conn && act_conn->neon);
    } else if (data->luminosity_lux < hw->lux_low_lx) {
        set_actuator(ctx, ACT_UV, hw->uv_gpio, 1, act_conn && act_conn->uv);
        set_actuator(ctx, ACT_NEON, hw->neon_gpio, 1, act_conn && act_conn->neon);
    } else if (data->luminosity_lux > hw->lux_high_lx) {
        set_actuator(ctx, ACT_UV, hw->uv_gpio, 0, act_conn && act_conn->uv);
        set_actuator(ctx, ACT_NEON, hw->neon_gpio, 0, act_conn && act_conn->neon);
    }

    if (!have_co2) {
        set_actuator(ctx, ACT_FAN, hw->fan_gpio, 0, act_conn && act_conn->fan);
    } else if (data->co2_ppm > hw->co2_high_ppm) {
        set_actuator(ctx, ACT_FAN, hw->fan_gpio, 1, act_conn && act_conn->fan);
    } else {
        set_actuator(ctx, ACT_FAN, hw->fan_gpio, 0, act_conn && act_conn->fan);
    }

    return ESP_OK;
}
