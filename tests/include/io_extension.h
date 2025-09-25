#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "i2c.h"

typedef struct _io_extension_obj_t {
    i2c_master_dev_handle_t addr;
    uint8_t Last_io_value;
    uint8_t Last_od_value;
} io_extension_obj_t;

static inline esp_err_t IO_EXTENSION_Init(void)
{
    return ESP_OK;
}

static inline esp_err_t IO_EXTENSION_Output(uint8_t pin, uint8_t value)
{
    (void)pin;
    (void)value;
    return ESP_OK;
}

static inline esp_err_t IO_EXTENSION_Input(uint8_t pin, uint8_t *value)
{
    (void)pin;
    if (value)
    {
        *value = 0;
    }
    return ESP_OK;
}

static inline esp_err_t IO_EXTENSION_Pwm_Output(uint8_t value)
{
    (void)value;
    return ESP_OK;
}

static inline esp_err_t IO_EXTENSION_Adc_Input(uint16_t *value)
{
    if (value)
    {
        *value = 0;
    }
    return ESP_OK;
}
