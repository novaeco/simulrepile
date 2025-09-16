#include "env_control.h"
#include "sensors.h"
#include "gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <math.h>

static TimerHandle_t s_timer = NULL;
static reptile_env_thresholds_t s_thr;
static reptile_env_state_t s_state;
static reptile_env_update_cb_t s_cb = NULL;
static void *s_cb_ctx = NULL;
static bool s_pump_auto = false;
static bool s_heat_auto = false;
static TaskHandle_t s_pump_task = NULL;
static TaskHandle_t s_heat_task = NULL;

static void notify_state(void)
{
    if (s_cb)
    {
        s_cb(&s_state, s_cb_ctx);
    }
}

static void pump_task(void *arg)
{
    (void)arg;
    s_state.pumping = true;
    notify_state();
    reptile_water_gpio();
    s_state.pumping = false;
    notify_state();
    s_pump_task = NULL;
    vTaskDelete(NULL);
}

static void heat_task(void *arg)
{
    (void)arg;
    s_state.heating = true;
    notify_state();
    reptile_heat_gpio();
    s_state.heating = false;
    notify_state();
    s_heat_task = NULL;
    vTaskDelete(NULL);
}

static void timer_cb(TimerHandle_t t)
{
    (void)t;
    float temp = sensors_read_temperature();
    float hum  = sensors_read_humidity();
    s_state.temperature = temp;
    s_state.humidity = hum;

    if (!isnan(temp))
    {
        if (temp <= s_thr.temp_setpoint - 1)
            s_heat_auto = true;
        else if (temp >= s_thr.temp_setpoint + 1)
            s_heat_auto = false;
    }
    if (!isnan(hum))
    {
        if (hum <= s_thr.humidity_setpoint - 5)
            s_pump_auto = true;
        else if (hum >= s_thr.humidity_setpoint + 5)
            s_pump_auto = false;
    }

    if (s_heat_auto && s_heat_task == NULL)
    {
        xTaskCreate(heat_task, "heat_task", 2048, NULL, 5, &s_heat_task);
    }
    if (s_pump_auto && s_pump_task == NULL)
    {
        xTaskCreate(pump_task, "pump_task", 2048, NULL, 5, &s_pump_task);
    }

    notify_state();
}

esp_err_t reptile_env_start(const reptile_env_thresholds_t *thr,
                            reptile_env_update_cb_t cb,
                            void *user_ctx)
{
    if (s_timer)
    {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = sensors_init();
    if (err != ESP_OK)
    {
        return err;
    }
    s_thr = *thr;
    s_cb = cb;
    s_cb_ctx = user_ctx;
    s_state.temperature = NAN;
    s_state.humidity = NAN;
    s_state.heating = false;
    s_state.pumping = false;
    s_pump_auto = false;
    s_heat_auto = false;
    const TickType_t period = pdMS_TO_TICKS(1000);
    s_timer = xTimerCreate("env_ctrl", period, pdTRUE, NULL, timer_cb);
    if (!s_timer)
    {
        return ESP_ERR_NO_MEM;
    }
    if (xTimerStart(s_timer, 0) != pdPASS)
    {
        xTimerDelete(s_timer, 0);
        s_timer = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void reptile_env_stop(void)
{
    if (s_timer)
    {
        xTimerStop(s_timer, portMAX_DELAY);
        xTimerDelete(s_timer, portMAX_DELAY);
        s_timer = NULL;
    }
}

void reptile_env_set_thresholds(const reptile_env_thresholds_t *thr)
{
    s_thr = *thr;
}

void reptile_env_get_state(reptile_env_state_t *out)
{
    if (out)
        *out = s_state;
}

void reptile_env_manual_pump(void)
{
    if (s_pump_task == NULL)
    {
        xTaskCreate(pump_task, "pump_task", 2048, NULL, 5, &s_pump_task);
    }
}

void reptile_env_manual_heat(void)
{
    if (s_heat_task == NULL)
    {
        xTaskCreate(heat_task, "heat_task", 2048, NULL, 5, &s_heat_task);
    }
}

