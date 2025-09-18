#include "ch422g.h"

#include "esp_check.h"
#include "esp_log.h"
#include "i2c.h"

#define TAG "ch422g"
#define CH422G_XFER_TIMEOUT_MS 50

static i2c_master_dev_handle_t s_dev = NULL;
static uint8_t s_shadow = 0xFFu;

static esp_err_t ch422g_write_shadow(void)
{
    uint8_t payload[2] = {CH422G_REG_EXIO, s_shadow};
    return i2c_master_transmit(s_dev, payload, sizeof(payload), CH422G_XFER_TIMEOUT_MS);
}

uint8_t ch422g_exio_shadow_get(void)
{
    return s_shadow;
}

esp_err_t ch422g_init(void)
{
    if (s_dev != NULL) {
        return ESP_OK;
    }

    DEV_I2C_Port port = DEV_I2C_Init();
    ESP_RETURN_ON_FALSE(port.bus != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C bus unavailable");

    esp_err_t ret = DEV_I2C_Probe(CH422G_DEFAULT_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "No ACK from CH422G at 0x%02X: %s. Check 3V3 supply, SDA=%d,"
                 " SCL=%d and external pull-ups (2.2k–4.7kΩ).",
                 CH422G_DEFAULT_ADDR, esp_err_to_name(ret),
                 CONFIG_I2C_MASTER_SDA_GPIO, CONFIG_I2C_MASTER_SCL_GPIO);
        return ret;
    }

    ret = DEV_I2C_Set_Slave_Addr(&s_dev, CH422G_DEFAULT_ADDR);
    ESP_RETURN_ON_ERROR(ret, TAG, "attach CH422G");

    /* Force all EXIO outputs high so that downstream peripherals stay
     * deselected until explicitly toggled. */
    s_shadow = 0xFFu;
    ret = ch422g_write_shadow();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CH422G ready on 0x%02X", CH422G_DEFAULT_ADDR);
    } else {
        ESP_LOGE(TAG, "Failed to initialise CH422G outputs: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t ch422g_exio_set(uint8_t exio_index, bool level)
{
    ESP_RETURN_ON_FALSE(exio_index >= 1u && exio_index <= 8u, ESP_ERR_INVALID_ARG, TAG,
                        "invalid EXIO%u", (unsigned)exio_index);
    ESP_RETURN_ON_ERROR(ch422g_init(), TAG, "initialise");

    uint8_t mask = 1u << (exio_index - 1u);
    if (level) {
        s_shadow |= mask;
    } else {
        s_shadow &= (uint8_t)~mask;
    }

    esp_err_t ret = ch422g_write_shadow();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update EXIO%u: %s", (unsigned)exio_index, esp_err_to_name(ret));
    }
    return ret;
}
