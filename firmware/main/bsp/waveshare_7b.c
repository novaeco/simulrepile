#include "bsp/waveshare_7b.h"

#include "bsp/exio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "hal/ledc_types.h"

static const char *TAG = "waveshare_7b";

static bool s_backlight_enabled = false;
static uint8_t s_backlight_percent = 0;

static esp_err_t configure_rgb_panel(void)
{
    ESP_LOGI(TAG, "Configuring RGB interface pins");
    return ESP_OK;
}

static esp_err_t configure_touch(void)
{
    ESP_LOGI(TAG, "Configuring capacitive touch controller stub");
    return ESP_OK;
}

static esp_err_t configure_sd(void)
{
    ESP_LOGI(TAG, "Configuring SDMMC host stub");
    return ESP_OK;
}

static esp_err_t configure_usb(void)
{
    ESP_LOGI(TAG, "Configuring USB subsystem stub");
    return ESP_OK;
}

esp_err_t bsp_init(void)
{
    ESP_RETURN_ON_ERROR(exio_init(), TAG, "Failed to init EXIO");
    ESP_RETURN_ON_ERROR(exio_enable_lcd_vdd(true), TAG, "LCD VDD enable failed");
    ESP_RETURN_ON_ERROR(configure_rgb_panel(), TAG, "RGB panel init failed");
    ESP_RETURN_ON_ERROR(configure_touch(), TAG, "Touch init failed");
    ESP_RETURN_ON_ERROR(configure_sd(), TAG, "SD init failed");
    ESP_RETURN_ON_ERROR(configure_usb(), TAG, "USB init failed");
    ESP_RETURN_ON_ERROR(bsp_backlight_enable(true), TAG, "Backlight enable failed");
    ESP_LOGI(TAG, "BSP initialized");
    return ESP_OK;
}

esp_err_t bsp_backlight_enable(bool enable)
{
    ESP_LOGI(TAG, "Backlight %s", enable ? "ON" : "OFF");
    s_backlight_enabled = enable;
    return exio_enable_display(enable);
}

esp_err_t bsp_backlight_set(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_backlight_percent = percent;
    ESP_LOGI(TAG, "Backlight brightness: %u%%", percent);
    if (!s_backlight_enabled && percent > 0) {
        bsp_backlight_enable(true);
    }
    return ESP_OK;
}

esp_err_t bsp_battery_read_mv(uint16_t *millivolts)
{
    ESP_RETURN_ON_FALSE(millivolts, ESP_ERR_INVALID_ARG, TAG, "Null pointer");
    *millivolts = 3800 + s_backlight_percent; // Stub value varying with brightness
    return ESP_OK;
}

esp_err_t bsp_select_usb(bool usb_mode)
{
    ESP_LOGI(TAG, "Switching %s", usb_mode ? "USB" : "CAN");
    return exio_select_usb(usb_mode);
}
