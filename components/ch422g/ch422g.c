#include "ch422g.h"

#include <inttypes.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "i2c.h"

#define TAG "ch422g"
#define CH422G_XFER_TIMEOUT_MS 50

static i2c_master_dev_handle_t s_dev = NULL;
static uint8_t s_shadow = 0xFFu;
static uint8_t s_addr = CH422G_DEFAULT_ADDR;

static esp_err_t ch422g_write_shadow(void)
{
    ESP_RETURN_ON_FALSE(s_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "device handle not ready");
    uint8_t payload[2] = {CH422G_REG_EXIO, s_shadow};
    return i2c_master_transmit(s_dev, payload, sizeof(payload), CH422G_XFER_TIMEOUT_MS);
}

uint8_t ch422g_exio_shadow_get(void)
{
    return s_shadow;
}

static esp_err_t ch422g_detect_address(uint8_t *out_addr)
{
    DEV_I2C_Port port = DEV_I2C_Init();
    ESP_RETURN_ON_FALSE(port.bus != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C bus unavailable");
    ESP_RETURN_ON_FALSE(out_addr != NULL, ESP_ERR_INVALID_ARG, TAG, "missing output buffer");

    int64_t start_us = esp_timer_get_time();
    (void)esp_task_wdt_reset();
    esp_err_t ret = i2c_master_probe(port.bus, CH422G_DEFAULT_ADDR, 100);
    if (ret == ESP_OK) {
        *out_addr = CH422G_DEFAULT_ADDR;
        int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
        ESP_LOGI(TAG, "CH422G détecté à l'adresse par défaut en %" PRIi64 " ms", elapsed_ms);
        return ESP_OK;
    }

    ESP_LOGE(TAG,
             "No ACK from CH422G at 0x%02X: %s. Check 3V3 supply, SDA=%d, SCL=%d and external pull-ups (2.2k–4.7kΩ).",
             CH422G_DEFAULT_ADDR, esp_err_to_name(ret), CONFIG_I2C_MASTER_SDA_GPIO, CONFIG_I2C_MASTER_SCL_GPIO);

#if CONFIG_CH422G_AUTOSCAN_ADDRESSES
    ESP_LOGW(TAG, "Scanning alternative CH422G addresses between 0x%02X and 0x%02X", CH422G_ADDR_MIN,
             CH422G_ADDR_MAX);
    esp_err_t last_ret = ret;
    for (uint8_t addr = CH422G_ADDR_MIN; addr <= CH422G_ADDR_MAX; ++addr) {
        if (addr == CH422G_DEFAULT_ADDR) {
            continue;
        }

        (void)esp_task_wdt_reset();
        last_ret = i2c_master_probe(port.bus, addr, 100);
        if (last_ret == ESP_OK) {
            ESP_LOGW(TAG, "CH422G acknowledged at alternative address 0x%02X", addr);
            *out_addr = addr;
            int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
            ESP_LOGI(TAG, "CH422G détecté à l'adresse 0x%02X en %" PRIi64 " ms", addr, elapsed_ms);
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG,
             "CH422G remains unreachable after scan: %s. Verify wiring or configure the SD CS GPIO fallback.",
             esp_err_to_name(last_ret));
    return last_ret;
#else
    ESP_LOGE(TAG,
             "CH422G remains unreachable: %s. Verify wiring or configure the SD CS GPIO fallback.",
             esp_err_to_name(ret));
    (void)esp_task_wdt_reset();
    return ret;
#endif
}

esp_err_t ch422g_init(void)
{
    if (s_dev != NULL) {
        return ESP_OK;
    }

    DEV_I2C_Port port = DEV_I2C_Init();
    ESP_RETURN_ON_FALSE(port.bus != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C bus unavailable");

    int64_t start_us = esp_timer_get_time();
    (void)esp_task_wdt_reset();
    esp_err_t ret = ch422g_detect_address(&s_addr);
    if (ret != ESP_OK) {
        return ret;
    }

    (void)esp_task_wdt_reset();
    ret = DEV_I2C_Set_Slave_Addr(&s_dev, s_addr);
    ESP_RETURN_ON_ERROR(ret, TAG, "attach CH422G");

    /* Force all EXIO outputs high so that downstream peripherals stay
     * deselected until explicitly toggled. */
    s_shadow = 0xFFu;
    ret = ch422g_write_shadow();
    if (ret == ESP_OK) {
        int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
        ESP_LOGI(TAG, "CH422G ready on 0x%02X", s_addr);
        ESP_LOGD(TAG, "Initialisation CH422G en %" PRIi64 " ms", elapsed_ms);
    } else {
        ESP_LOGE(TAG, "Failed to initialise CH422G outputs: %s", esp_err_to_name(ret));
    }

    (void)esp_task_wdt_reset();
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
        ESP_LOGE(TAG, "Failed to update EXIO%u via CH422G@0x%02X: %s", (unsigned)exio_index, s_addr,
                 esp_err_to_name(ret));
    }
    return ret;
}
