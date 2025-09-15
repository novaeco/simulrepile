#pragma once

#include <stdint.h>

#include "hal/gpio_hal.h"
#include "esp_idf_version.h"

#ifndef gpio_hal_iomux_func_sel
#include "soc/io_mux_reg.h"
#include "soc/soc.h"
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "soc/usb_serial_jtag_reg.h"
#endif

static inline void gpio_hal_iomux_func_sel(uint32_t pin_name, uint32_t func)
{
#if defined(USB_SERIAL_JTAG_CONF0_REG) && defined(IO_MUX_GPIO19_REG) && defined(IO_MUX_GPIO20_REG)
    if (pin_name == IO_MUX_GPIO19_REG || pin_name == IO_MUX_GPIO20_REG) {
        CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    }
#endif
    PIN_FUNC_SELECT(pin_name, func);
}
#endif // gpio_hal_iomux_func_sel

#if defined(gpio_hal_func_sel)
#ifndef ESP_IDF_GPIO_HAL_FUNC_SEL_COMPAT
#define ESP_IDF_GPIO_HAL_FUNC_SEL_COMPAT
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
#undef gpio_hal_func_sel

#define ESP_IDF_GPIO_HAL_FUNC_SEL_GET_MACRO(_1, _2, _3, NAME, ...) NAME
#define ESP_IDF_GPIO_HAL_FUNC_SEL_2ARGS(pin_name, func) gpio_hal_iomux_func_sel((uint32_t)(pin_name), (func))
#define ESP_IDF_GPIO_HAL_FUNC_SEL_3ARGS(hal, gpio_num, func) gpio_ll_func_sel((hal)->dev, (gpio_num), (func))

#define gpio_hal_func_sel(...) \
    ESP_IDF_GPIO_HAL_FUNC_SEL_GET_MACRO(__VA_ARGS__, \
                                        ESP_IDF_GPIO_HAL_FUNC_SEL_3ARGS, \
                                        ESP_IDF_GPIO_HAL_FUNC_SEL_2ARGS)(__VA_ARGS__)
#endif // ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
#endif // ESP_IDF_GPIO_HAL_FUNC_SEL_COMPAT
#endif // defined(gpio_hal_func_sel)
