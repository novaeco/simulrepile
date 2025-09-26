#include "bsp/exio.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "exio";

static bool s_initialized = false;
static uint8_t s_output_state = 0;

esp_err_t exio_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Initializing CH422G virtual lines");
    s_output_state = 0;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t exio_set(exio_line_t line, bool level)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "EXIO not initialized");
    if (line >= 8) {
        return ESP_ERR_INVALID_ARG;
    }
    if (level) {
        s_output_state |= (1U << line);
    } else {
        s_output_state &= ~(1U << line);
    }
    ESP_LOGD(TAG, "line %d -> %d (state=0x%02x)", line, level, s_output_state);
    return ESP_OK;
}

esp_err_t exio_enable_display(bool enable)
{
    return exio_set(EXIO_LINE_DISP, enable);
}

esp_err_t exio_enable_lcd_vdd(bool enable)
{
    return exio_set(EXIO_LINE_LCD_VDD_EN, enable);
}

esp_err_t exio_select_usb(bool enable_usb)
{
    return exio_set(EXIO_LINE_USB_SEL, enable_usb);
}
