#include "ch422g.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c.h"

#define TAG "ch422g"
#define CH422G_XFER_TIMEOUT_MS 50
#define CH422G_PROBE_TIMEOUT_MS 100
#define CH422G_SCAN_MIN_ADDR 0x20u
#define CH422G_SCAN_MAX_ADDR 0x23u
#define CH422G_RETRY_DELAY_MS 12

static i2c_master_dev_handle_t s_dev = NULL;
static uint8_t s_addr;
static uint8_t s_shadow = 0xFFu;

static uint8_t ch422g_configured_addr(void)
{
    static uint8_t s_configured_addr;
    static bool s_configured_addr_initialised = false;
    static bool s_warned_8bit_address = false;
    static bool s_warned_out_of_range = false;

    if (!s_configured_addr_initialised) {
        uint8_t configured = (uint8_t)CONFIG_CH422G_I2C_ADDRESS;

        if (configured >= 0x40 && configured <= 0x47) {
            uint8_t normalised = (uint8_t)(configured >> 1);
            if (!s_warned_8bit_address) {
                ESP_LOGW(TAG,
                         "CONFIG_CH422G_I2C_ADDRESS=0x%02X corresponds to the 8-bit "
                         "datasheet value; normalising to 7-bit address 0x%02X.",
                         configured, normalised);
                s_warned_8bit_address = true;
            }
            configured = normalised;
        }

        if ((configured < 0x08 || configured > 0x77) && !s_warned_out_of_range) {
            uint8_t clamped = configured;
            if (configured < 0x08) {
                clamped = 0x08;
            } else if (configured > 0x77) {
                clamped = 0x77;
            }
            ESP_LOGW(TAG,
                     "CONFIG_CH422G_I2C_ADDRESS=0x%02X lies outside the 7-bit range "
                     "0x08–0x77. Clamping to 0x%02X.",
                     configured, clamped);
            configured = clamped;
            s_warned_out_of_range = true;
        }

        s_configured_addr = configured & 0x7Fu;
        s_configured_addr_initialised = true;
    }

    return s_configured_addr;
}

static bool ch422g_scheduler_started(void)
{
    return xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
}

static void ch422g_retry_delay(void)
{
    if (ch422g_scheduler_started()) {
        static bool s_wdt_status_warned = false;

        vTaskDelay(pdMS_TO_TICKS(CH422G_RETRY_DELAY_MS));

        esp_err_t status = esp_task_wdt_status(NULL);
        if (status == ESP_OK) {
            esp_err_t wdt_ret = esp_task_wdt_reset();
            if (wdt_ret != ESP_OK && wdt_ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "esp_task_wdt_reset failed during retry delay: %s", esp_err_to_name(wdt_ret));
            }
        } else if (status != ESP_ERR_NOT_FOUND && status != ESP_ERR_INVALID_STATE && !s_wdt_status_warned) {
            ESP_LOGW(TAG, "esp_task_wdt_status failed during retry delay: %s", esp_err_to_name(status));
            s_wdt_status_warned = true;
        }
    } else {
        esp_rom_delay_us(CH422G_RETRY_DELAY_MS * 1000);
    }
}

static esp_err_t ch422g_probe_address(uint8_t addr)
{
    DEV_I2C_Port port = DEV_I2C_Init();
    if (port.bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_FAIL;
    const int max_attempts = 2;

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        ret = i2c_master_probe(port.bus, addr, CH422G_PROBE_TIMEOUT_MS);
        if (ret == ESP_OK) {
            return ESP_OK;
        }

        bool recoverable = (ret == ESP_ERR_TIMEOUT) || (ret == ESP_ERR_INVALID_STATE);
        if (!recoverable || attempt == max_attempts) {
            break;
        }

        ESP_LOGW(TAG,
                 "I2C probe 0x%02X attempt %d/%d failed (%s). Recovering bus before retry.",
                 addr, attempt, max_attempts, esp_err_to_name(ret));
        ESP_ERROR_CHECK_WITHOUT_ABORT(DEV_I2C_Bus_Recover());
        port = DEV_I2C_Init();
        if (port.bus == NULL) {
            return ESP_ERR_INVALID_STATE;
        }
        ch422g_retry_delay();
    }

    return ret;
}

esp_err_t ch422g_scan(uint8_t start_addr, uint8_t end_addr, uint8_t *out_addr)
{
    if (start_addr > end_addr) {
        uint8_t tmp = start_addr;
        start_addr = end_addr;
        end_addr = tmp;
    }

    esp_err_t last_err = ESP_ERR_NOT_FOUND;
    uint8_t detected = 0;

    for (uint16_t addr = start_addr; addr <= end_addr; ++addr) {
        if (addr < 0x08 || addr > 0x77) {
            continue;
        }

        esp_err_t ret = ch422g_probe_address((uint8_t)addr);
        if (ret == ESP_OK) {
            detected = (uint8_t)addr;
            if (out_addr != NULL) {
                *out_addr = detected;
            }
            return ESP_OK;
        }

        last_err = ret;
        ch422g_retry_delay();
    }

    if (last_err == ESP_OK || last_err == ESP_ERR_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    return last_err;
}

static esp_err_t ch422g_write_shadow(void)
{
    uint8_t payload[2] = {CH422G_REG_EXIO, s_shadow};
    return i2c_master_transmit(s_dev, payload, sizeof(payload), CH422G_XFER_TIMEOUT_MS);
}

uint8_t ch422g_exio_shadow_get(void)
{
    return s_shadow;
}

uint8_t ch422g_get_address(void)
{
    if (s_addr == 0u) {
        return ch422g_configured_addr();
    }
    return s_addr;
}

esp_err_t ch422g_init(void)
{
    if (s_dev != NULL) {
        return ESP_OK;
    }

    DEV_I2C_Port port = DEV_I2C_Init();
    ESP_RETURN_ON_FALSE(port.bus != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C bus unavailable");

    uint8_t configured_addr = ch422g_configured_addr();
    s_addr = configured_addr;

    uint8_t detected_addr = configured_addr;
    esp_err_t ret = ch422g_scan(CH422G_SCAN_MIN_ADDR, CH422G_SCAN_MAX_ADDR, &detected_addr);
    if (ret == ESP_ERR_NOT_FOUND &&
        (configured_addr < CH422G_SCAN_MIN_ADDR || configured_addr > CH422G_SCAN_MAX_ADDR)) {
        ret = ch422g_scan(configured_addr, configured_addr, &detected_addr);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "No ACK from CH422G between 0x%02X and 0x%02X (configured 0x%02X): %s. "
                 "Check 3V3 supply, SDA=%d, SCL=%d and external pull-ups (2.2k–4.7kΩ).",
                 CH422G_SCAN_MIN_ADDR, CH422G_SCAN_MAX_ADDR, configured_addr,
                 esp_err_to_name(ret), CONFIG_I2C_MASTER_SDA_GPIO, CONFIG_I2C_MASTER_SCL_GPIO);
        return ret;
    }

    s_addr = detected_addr;

    if (s_addr != configured_addr) {
        ESP_LOGW(TAG,
                 "CH422G responded on 0x%02X instead of configured 0x%02X. Verify A0/A1 straps "
                 "or update CONFIG_CH422G_I2C_ADDRESS.",
                 s_addr, configured_addr);
    }

    ret = DEV_I2C_Set_Slave_Addr(&s_dev, s_addr);
    ESP_RETURN_ON_ERROR(ret, TAG, "attach CH422G");

    /* Force all EXIO outputs high so that downstream peripherals stay
     * deselected until explicitly toggled. */
    s_shadow = 0xFFu;
    ret = ch422g_write_shadow();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CH422G ready on 0x%02X", s_addr);
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
    uint8_t prev_shadow = s_shadow;
    if (level) {
        s_shadow |= mask;
    } else {
        s_shadow &= (uint8_t)~mask;
    }

    esp_err_t update_ret = ch422g_write_shadow();
    if (update_ret != ESP_OK) {
        s_shadow = prev_shadow;
        ESP_LOGE(TAG, "Failed to update EXIO%u: %s", (unsigned)exio_index, esp_err_to_name(update_ret));
    }
    return update_ret;
}
