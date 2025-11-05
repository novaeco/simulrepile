#include "bsp/exio.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/pins_touch.h"

static const char *TAG = "exio";

#define CH422G_I2C_ADDRESS       0x24
#define CH422G_REG_MODE          0x02
#define CH422G_REG_OUTPUT        0x03
#define CH422G_REG_INPUT         0x04
#define CH422G_REG_PWM           0x05
#define CH422G_REG_ADC           0x06

#define EXIO_I2C_FREQUENCY_HZ    (400 * 1000)
#define EXIO_I2C_TIMEOUT_MS      100

static bool s_initialized = false;
static uint8_t s_output_state = 0;
static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t ch422g_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(s_dev, payload, sizeof(payload), EXIO_I2C_TIMEOUT_MS);
}

esp_err_t exio_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing CH422G IO expander");

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = TOUCH_PIN_SDA,
        .scl_io_num = TOUCH_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "failed to create I2C bus");

    i2c_device_config_t dev_cfg = {
        .scl_speed_hz = EXIO_I2C_FREQUENCY_HZ,
    };
    dev_cfg.device_address = CH422G_I2C_ADDRESS;
#ifdef I2C_ADDR_BIT_LEN_7
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
#endif
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), TAG, "failed to add CH422G");

    vTaskDelay(pdMS_TO_TICKS(2));

    ESP_RETURN_ON_ERROR(ch422g_write_reg(CH422G_REG_MODE, 0xFF), TAG, "mode write failed");
    s_output_state = 0x00;
    ESP_RETURN_ON_ERROR(ch422g_write_reg(CH422G_REG_OUTPUT, s_output_state), TAG, "output write failed");
    ESP_RETURN_ON_ERROR(ch422g_write_reg(CH422G_REG_PWM, 0x00), TAG, "pwm reset failed");

    s_initialized = true;
    return ESP_OK;
}

esp_err_t exio_set(exio_line_t line, bool level)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "EXIO not initialized");
    ESP_RETURN_ON_FALSE(line < 8, ESP_ERR_INVALID_ARG, TAG, "invalid line");

    if (level) {
        s_output_state |= (1U << line);
    } else {
        s_output_state &= ~(1U << line);
    }

    ESP_RETURN_ON_ERROR(ch422g_write_reg(CH422G_REG_OUTPUT, s_output_state), TAG, "output sync failed");
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
    /* Active low: 0 selects USB, 1 selects CAN */
    return exio_set(EXIO_LINE_USB_SEL, enable_usb ? 0 : 1);
}

esp_err_t exio_set_pwm(uint8_t percent)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "EXIO not initialized");
    if (percent > 100) {
        percent = 100;
    }
    /* Limit to avoid turning the LED driver fully off as per Waveshare reference */
    if (percent >= 97) {
        percent = 97;
    }
    uint8_t duty = (uint8_t)((percent * 255U) / 100U);
    return ch422g_write_reg(CH422G_REG_PWM, duty);
}

i2c_master_bus_handle_t exio_get_bus_handle(void)
{
    return s_bus;
}
