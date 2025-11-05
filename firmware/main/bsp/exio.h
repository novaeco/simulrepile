#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EXIO_LINE_DISP = 2,
    EXIO_LINE_LCD_VDD_EN = 6,
    EXIO_LINE_USB_SEL = 5,
    EXIO_LINE_TOUCH_RST = 1,
} exio_line_t;

esp_err_t exio_init(void);
esp_err_t exio_set(exio_line_t line, bool level);
esp_err_t exio_enable_display(bool enable);
esp_err_t exio_enable_lcd_vdd(bool enable);
esp_err_t exio_select_usb(bool enable_usb);
esp_err_t exio_set_pwm(uint8_t percent);
i2c_master_bus_handle_t exio_get_bus_handle(void);

#ifdef __cplusplus
}
#endif
