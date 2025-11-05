#include "bsp/waveshare_7b.h"

#include "bsp/exio.h"
#include "bsp/pins_lcd.h"
#include "bsp/pins_sd.h"
#include "bsp/pins_touch.h"
#include "bsp/pins_usb.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/sdmmc_host.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal/ledc_types.h"
#include "hal/usb_phy_types.h"

#include "sdmmc_cmd.h"
#include "usb/usb_phy.h"

#include "sdkconfig.h"

extern esp_err_t waveshare_7b_lgfx_init(void);

static const char *TAG = "waveshare_7b";

static bool s_backlight_enabled = false;
static uint8_t s_backlight_percent = 0;
static bool s_pwm_initialized = false;
static i2c_master_dev_handle_t s_touch_dev = NULL;
static bool s_touch_isr_attached = false;
static sdmmc_card_t *s_sd_card = NULL;
static usb_phy_handle_t s_usb_phy = NULL;

#define BACKLIGHT_LEDC_TIMER        LEDC_TIMER_0
#define BACKLIGHT_LEDC_MODE         LEDC_LOW_SPEED_MODE
#define BACKLIGHT_LEDC_CHANNEL      LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_RESOLUTION   LEDC_TIMER_12_BIT
#define BACKLIGHT_LEDC_FREQUENCY_HZ 5000
#define TOUCH_I2C_TIMEOUT_MS        100

static void IRAM_ATTR touch_irq_handler(void *arg)
{
    (void)arg;
}

static esp_err_t configure_rgb_panel(void)
{
    ESP_RETURN_ON_ERROR(waveshare_7b_lgfx_init(), TAG, "LovyanGFX init failed");

#if LCD_PIN_BACKLIGHT >= 0
    if (!s_pwm_initialized) {
        ledc_timer_config_t timer_cfg = {
            .speed_mode = BACKLIGHT_LEDC_MODE,
            .duty_resolution = BACKLIGHT_LEDC_RESOLUTION,
            .timer_num = BACKLIGHT_LEDC_TIMER,
            .freq_hz = BACKLIGHT_LEDC_FREQUENCY_HZ,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "LEDC timer config failed");

        ledc_channel_config_t channel_cfg = {
            .speed_mode = BACKLIGHT_LEDC_MODE,
            .channel = BACKLIGHT_LEDC_CHANNEL,
            .timer_sel = BACKLIGHT_LEDC_TIMER,
            .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = LCD_PIN_BACKLIGHT,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_cfg), TAG, "LEDC channel config failed");
        s_pwm_initialized = true;
    }
#endif

    ESP_LOGI(TAG, "LovyanGFX RGB panel configured");
    return ESP_OK;
}

static esp_err_t configure_touch(void)
{
    i2c_master_bus_handle_t bus = exio_get_bus_handle();
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "Touch bus unavailable");

    if (s_touch_dev == NULL) {
        i2c_device_config_t touch_cfg = {
            .scl_speed_hz = 400 * 1000,
        };
        touch_cfg.device_address = 0x38;
#ifdef I2C_ADDR_BIT_LEN_7
        touch_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
#endif
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &touch_cfg, &s_touch_dev), TAG, "Touch device add failed");
    }

    ESP_RETURN_ON_ERROR(exio_set(EXIO_LINE_TOUCH_RST, false), TAG, "Touch reset assert failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(exio_set(EXIO_LINE_TOUCH_RST, true), TAG, "Touch reset release failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    gpio_config_t irq_cfg = {
        .pin_bit_mask = 1ULL << TOUCH_PIN_IRQ,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&irq_cfg), TAG, "Touch IRQ config failed");

    if (!s_touch_isr_attached) {
        esp_err_t isr_err = gpio_install_isr_service(0);
        if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "GPIO ISR service failed: %s", esp_err_to_name(isr_err));
            return isr_err;
        }
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(TOUCH_PIN_IRQ, touch_irq_handler, NULL), TAG, "Touch ISR add failed");
        s_touch_isr_attached = true;
    }

    uint8_t reg = 0xA8;
    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_touch_dev, &reg, 1, &id, 1, TOUCH_I2C_TIMEOUT_MS), TAG, "Read touch ID failed");
    ESP_LOGI(TAG, "FT5x06 ID: 0x%02X", id);
    return ESP_OK;
}

static esp_err_t configure_sd(void)
{
    if (s_sd_card) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = CONFIG_BSP_SD_BUS_WIDTH_4BIT ? SDMMC_HOST_FLAG_4BIT : 0;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = SD_PIN_CLK;
    slot_config.cmd = SD_PIN_CMD;
    slot_config.d0 = SD_PIN_D0;
    slot_config.d1 = SD_PIN_D1;
    slot_config.d2 = SD_PIN_D2;
    slot_config.d3 = SD_PIN_D3;
    slot_config.width = CONFIG_BSP_SD_BUS_WIDTH_4BIT ? 4 : 1;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 12,
        .allocation_unit_size = 32 * 1024,
    };

    ESP_RETURN_ON_ERROR(esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &s_sd_card), TAG, "SD mount failed");
    ESP_LOGI(TAG, "SD card mounted: %.6s", s_sd_card->cid.name);
    return ESP_OK;
}

static esp_err_t configure_usb(void)
{
    if (!s_usb_phy) {
        usb_phy_otg_io_conf_t otg_conf = USB_PHY_SELF_POWERED_DEVICE(-1);
        usb_phy_config_t phy_conf = {
            .controller = USB_PHY_CONTROLLER_0,
            .target = USB_PHY_TARGET_INT,
            .otg_mode = USB_OTG_MODE_DEVICE,
            .otg_speed = USB_PHY_SPEED_FULL,
            .ext_io_conf = NULL,
            .otg_io_conf = &otg_conf,
        };
        ESP_RETURN_ON_ERROR(usb_new_phy(&phy_conf, &s_usb_phy), TAG, "USB PHY init failed");
        ESP_RETURN_ON_ERROR(usb_phy_otg_set_mode(s_usb_phy, USB_OTG_MODE_DEVICE), TAG, "USB set mode failed");
        ESP_RETURN_ON_ERROR(usb_phy_otg_dev_set_speed(s_usb_phy, USB_PHY_SPEED_FULL), TAG, "USB set speed failed");
    }

    ESP_RETURN_ON_ERROR(exio_select_usb(true), TAG, "USB select failed");
    ESP_LOGI(TAG, "USB PHY ready in device mode");
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
    if (enable) {
        ESP_RETURN_ON_ERROR(exio_enable_display(true), TAG, "Display enable failed");
        ESP_RETURN_ON_ERROR(exio_set_pwm(s_backlight_percent), TAG, "PWM restore failed");
    } else {
        ESP_RETURN_ON_ERROR(exio_set_pwm(0), TAG, "PWM disable failed");
        ESP_RETURN_ON_ERROR(exio_enable_display(false), TAG, "Display disable failed");
    }
    return ESP_OK;
}

esp_err_t bsp_backlight_set(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_backlight_percent = percent;
    ESP_LOGI(TAG, "Backlight brightness: %u%%", percent);
    if (!s_backlight_enabled && percent > 0) {
        ESP_RETURN_ON_ERROR(bsp_backlight_enable(true), TAG, "Backlight auto-enable failed");
    }

#if LCD_PIN_BACKLIGHT >= 0
    if (s_pwm_initialized) {
        uint32_t duty = ((1U << BACKLIGHT_LEDC_RESOLUTION) - 1U) * percent / 100U;
        ESP_RETURN_ON_ERROR(ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty), TAG, "LEDC set duty failed");
        ESP_RETURN_ON_ERROR(ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL), TAG, "LEDC update failed");
    }
#endif
    ESP_RETURN_ON_ERROR(exio_set_pwm(percent), TAG, "PWM update failed");
    return ESP_OK;
}

esp_err_t bsp_battery_read_mv(uint16_t *millivolts)
{
    ESP_RETURN_ON_FALSE(millivolts, ESP_ERR_INVALID_ARG, TAG, "Null pointer");
    *millivolts = 3800 + s_backlight_percent;
    return ESP_OK;
}

esp_err_t bsp_select_usb(bool usb_mode)
{
    ESP_LOGI(TAG, "Switching %s", usb_mode ? "USB" : "CAN");
    return exio_select_usb(usb_mode);
}
