#pragma once

#include <stdint.h>

#include "hal/gpio_hal.h"

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
