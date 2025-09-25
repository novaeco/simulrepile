#include "gpio.h"

#include <limits.h>

#include "waveshare_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "gpio_real"

typedef enum {
    REPTILE_OUTPUT_NONE = 0,
    REPTILE_OUTPUT_GPIO,
    REPTILE_OUTPUT_EXPANDER,
} reptile_output_bus_t;

typedef struct {
    reptile_output_bus_t bus;
    bool active_high;
    union {
        gpio_num_t gpio;
        uint8_t line;
    } signal;
} reptile_output_t;

typedef struct {
    reptile_output_t heater;
    reptile_output_t pump;
    reptile_output_t uv;
} reptile_channel_hw_t;

enum {
    HEAT_RES_LINE = WAVESHARE_IO_LINE_FROM_EXIO_CONST(HEAT_RES_EXIO),
    WATER_PUMP_LINE = WAVESHARE_IO_LINE_FROM_EXIO_CONST(WATER_PUMP_EXIO),
    TEST_HEATER_LINE = WAVESHARE_IO_LINE_FROM_EXIO_CONST(1),
    TEST_PUMP_LINE = WAVESHARE_IO_LINE_FROM_EXIO_CONST(2),
    TEST_UV_LINE = WAVESHARE_IO_LINE_FROM_EXIO_CONST(3),
};

static const reptile_channel_hw_t s_hw_map[] = {
    {
        .heater = {.bus = REPTILE_OUTPUT_EXPANDER, .active_high = false, .signal.line = HEAT_RES_LINE},
        .pump = {.bus = REPTILE_OUTPUT_EXPANDER, .active_high = false, .signal.line = WATER_PUMP_LINE},
        .uv = {.bus = REPTILE_OUTPUT_GPIO, .active_high = true, .signal.gpio = LED_GPIO_PIN},
    },
    {
        .heater = {.bus = REPTILE_OUTPUT_EXPANDER, .active_high = false, .signal.line = TEST_HEATER_LINE},
        .pump = {.bus = REPTILE_OUTPUT_EXPANDER, .active_high = false, .signal.line = TEST_PUMP_LINE},
        .uv = {.bus = REPTILE_OUTPUT_EXPANDER, .active_high = false, .signal.line = TEST_UV_LINE},
    },
};

#if SERVO_FEED_EXIO > 0
static const reptile_output_t s_feed_output = {
    .bus = REPTILE_OUTPUT_EXPANDER,
    .active_high = false,
    .signal.line = WAVESHARE_IO_LINE_FROM_EXIO_CONST(SERVO_FEED_EXIO),
};
#else
static const reptile_output_t s_feed_output = {
    .bus = REPTILE_OUTPUT_NONE,
    .active_high = false,
    .signal.line = UINT8_MAX,
};
#endif

static const size_t s_hw_channel_count = sizeof(s_hw_map) / sizeof(s_hw_map[0]);

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

static inline bool actuator_available(const reptile_output_t *out)
{
    return out && out->bus != REPTILE_OUTPUT_NONE;
}

static inline uint8_t actuator_gpio_level(const reptile_output_t *out, bool active)
{
    bool level_high = (out->active_high == active);
    return level_high ? 1u : 0u;
}

static esp_err_t actuator_drive(const reptile_output_t *out, bool active)
{
    if (!actuator_available(out)) {
        return ESP_ERR_INVALID_STATE;
    }

    switch (out->bus) {
    case REPTILE_OUTPUT_GPIO:
        gpio_set_level(out->signal.gpio, actuator_gpio_level(out, active));
        return ESP_OK;
    case REPTILE_OUTPUT_EXPANDER: {
        bool level = (out->active_high == active);
        if (!waveshare_io_line_valid(out->signal.line)) {
            return ESP_ERR_INVALID_ARG;
        }
        return waveshare_io_output_set(out->signal.line, level);
    }
    case REPTILE_OUTPUT_NONE:
    default:
        return ESP_ERR_INVALID_STATE;
    }
}

static void configure_idle_state(const reptile_output_t *out)
{
    if (!actuator_available(out)) {
        return;
    }
    if (out->bus == REPTILE_OUTPUT_GPIO) {
        gpio_real_mode(out->signal.gpio, GPIO_MODE_OUTPUT);
    }
    esp_err_t err = actuator_drive(out, false);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to set idle state (bus=%d, err=%s)", out->bus, esp_err_to_name(err));
    }
}

static bool channel_valid(size_t channel)
{
    if (channel < s_hw_channel_count) {
        return true;
    }
    ESP_LOGW(TAG,
             "Terrarium channel %zu out of range (configured %zu)",
             channel,
             s_hw_channel_count);
    return false;
}

static const reptile_channel_hw_t *get_channel(size_t channel)
{
    if (!channel_valid(channel)) {
        return NULL;
    }
    return &s_hw_map[channel];
}

static void gpio_real_feed(size_t channel)
{
    if (channel != 0) {
        ESP_LOGW(TAG, "Feed actuator not mapped for terrarium %zu", channel);
        return;
    }
    if (!actuator_available(&s_feed_output)) {
        ESP_LOGW(TAG, "Feed actuator unavailable for terrarium %zu", channel);
        return;
    }

    esp_err_t err = actuator_drive(&s_feed_output, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to enable feeder channel %zu: %s",
                 channel,
                 esp_err_to_name(err));
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    err = actuator_drive(&s_feed_output, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to disable feeder channel %zu: %s",
                 channel,
                 esp_err_to_name(err));
    }
}

static void gpio_real_water(size_t channel)
{
    const reptile_channel_hw_t *hw = get_channel(channel);
    if (!hw || !actuator_available(&hw->pump)) {
        ESP_LOGW(TAG, "Pump actuator unavailable for terrarium %zu", channel);
        return;
    }
    esp_err_t err = actuator_drive(&hw->pump, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable pump channel %zu: %s", channel, esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(REPTILE_GPIO_PUMP_PULSE_MS));
    err = actuator_drive(&hw->pump, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable pump channel %zu: %s", channel, esp_err_to_name(err));
    }
}

static void gpio_real_heat(size_t channel)
{
    const reptile_channel_hw_t *hw = get_channel(channel);
    if (!hw || !actuator_available(&hw->heater)) {
        ESP_LOGW(TAG, "Heater actuator unavailable for terrarium %zu", channel);
        return;
    }
    esp_err_t err = actuator_drive(&hw->heater, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable heater channel %zu: %s", channel, esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(REPTILE_GPIO_HEAT_PULSE_MS));
    err = actuator_drive(&hw->heater, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable heater channel %zu: %s", channel, esp_err_to_name(err));
    }
}

static void gpio_real_uv(size_t channel, bool on)
{
    const reptile_channel_hw_t *hw = get_channel(channel);
    if (!hw || !actuator_available(&hw->uv)) {
        ESP_LOGW(TAG, "UV actuator unavailable for terrarium %zu", channel);
        return;
    }
    esp_err_t err = actuator_drive(&hw->uv, on);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to set UV channel %zu to %s: %s",
                 channel,
                 on ? "ON" : "OFF",
                 esp_err_to_name(err));
    }
}

static esp_err_t gpio_real_init(void)
{
    bool need_expander = (actuator_available(&s_feed_output) &&
                          s_feed_output.bus == REPTILE_OUTPUT_EXPANDER);
    for (size_t i = 0; i < s_hw_channel_count; ++i) {
        const reptile_channel_hw_t *hw = &s_hw_map[i];
        const reptile_output_t *outputs[] = {&hw->heater, &hw->pump, &hw->uv};
        for (size_t j = 0; j < sizeof(outputs) / sizeof(outputs[0]); ++j) {
            const reptile_output_t *out = outputs[j];
            if (!actuator_available(out)) {
                continue;
            }
            if (out->bus == REPTILE_OUTPUT_EXPANDER) {
                need_expander = true;
            } else if (out->bus == REPTILE_OUTPUT_GPIO) {
                gpio_real_mode(out->signal.gpio, GPIO_MODE_OUTPUT);
            }
        }
    }

    if (need_expander) {
        ESP_RETURN_ON_ERROR(waveshare_io_init(), TAG, "init Waveshare IO expander");
    }

    for (size_t i = 0; i < s_hw_channel_count; ++i) {
        const reptile_channel_hw_t *hw = &s_hw_map[i];
        configure_idle_state(&hw->heater);
        configure_idle_state(&hw->pump);
        configure_idle_state(&hw->uv);
    }

    if (actuator_available(&s_feed_output)) {
        configure_idle_state(&s_feed_output);
    }

    return ESP_OK;
}

static void gpio_real_deinit(void)
{
    for (size_t i = 0; i < s_hw_channel_count; ++i) {
        const reptile_channel_hw_t *hw = &s_hw_map[i];
        if (actuator_available(&hw->heater)) {
            actuator_drive(&hw->heater, false);
            if (hw->heater.bus == REPTILE_OUTPUT_GPIO) {
                gpio_real_mode(hw->heater.signal.gpio, GPIO_MODE_INPUT);
            }
        }
        if (actuator_available(&hw->pump)) {
            actuator_drive(&hw->pump, false);
            if (hw->pump.bus == REPTILE_OUTPUT_GPIO) {
                gpio_real_mode(hw->pump.signal.gpio, GPIO_MODE_INPUT);
            }
        }
        if (actuator_available(&hw->uv)) {
            actuator_drive(&hw->uv, false);
            if (hw->uv.bus == REPTILE_OUTPUT_GPIO) {
                gpio_real_mode(hw->uv.signal.gpio, GPIO_MODE_INPUT);
            }
        }
    }

    if (actuator_available(&s_feed_output)) {
        esp_err_t err = actuator_drive(&s_feed_output, false);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG,
                     "Failed to release feeder channel: %s",
                     esp_err_to_name(err));
        }
    }
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
    .uv = gpio_real_uv,
    .deinit = gpio_real_deinit,
    .channel_count = sizeof(s_hw_map) / sizeof(s_hw_map[0]),
};
