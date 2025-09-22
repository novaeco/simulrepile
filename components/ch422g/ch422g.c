#include "ch422g.h"

#include <stdio.h>

#include "driver/gpio.h"
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
#define CH422G_SCAN_MAX_ADDR 0x27u
#define CH422G_RETRY_DELAY_MS 12

static i2c_master_dev_handle_t s_dev = NULL;
static uint8_t s_addr = CH422G_I2C_ADDR_DEFAULT;
static uint8_t s_shadow = 0xFFu;
static bool s_diag_logged = false;
static bool s_input_mode_warned = false;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#define GT911_ADDR_PRIMARY 0x5Du
#define GT911_ADDR_BACKUP 0x14u

static void ch422g_log_bus_snapshot(void)
{
    uint8_t detected[16] = {0};
    size_t found = 0;
    esp_err_t scan_ret = DEV_I2C_Scan(0x08u, 0x77u, detected, ARRAY_SIZE(detected), &found);

    if (scan_ret == ESP_OK || scan_ret == ESP_ERR_NOT_FOUND) {
        int sda_level = gpio_get_level(CONFIG_I2C_MASTER_SDA_GPIO);
        int scl_level = gpio_get_level(CONFIG_I2C_MASTER_SCL_GPIO);

        if (found == 0) {
            ESP_LOGW(TAG,
                     "I2C scan (0x08-0x77): aucun périphérique n'a répondu. SDA=%d SCL=%d.",
                     sda_level, scl_level);
            return;
        }

        size_t limit = found;
        if (limit > ARRAY_SIZE(detected)) {
            limit = ARRAY_SIZE(detected);
        }

        char list[ARRAY_SIZE(detected) * 6];
        list[0] = '\0';
        size_t offset = 0;
        bool has_gt911 = false;
        bool has_ch422g_candidate = false;

        for (size_t i = 0; i < limit; ++i) {
            uint8_t addr = detected[i];
            if (offset < sizeof(list)) {
                int written = snprintf(list + offset, sizeof(list) - offset, "0x%02X%s", addr,
                                       (i + 1 < limit) ? " " : "");
                if (written < 0) {
                    list[0] = '\0';
                    break;
                }
                if ((size_t)written >= sizeof(list) - offset) {
                    offset = sizeof(list) - 1;
                    break;
                } else {
                    offset += (size_t)written;
                }
            }

            if (addr >= CH422G_SCAN_MIN_ADDR && addr <= CH422G_SCAN_MAX_ADDR) {
                has_ch422g_candidate = true;
            }
            if (addr == GT911_ADDR_PRIMARY || addr == GT911_ADDR_BACKUP) {
                has_gt911 = true;
            }
        }

        if (limit < found && offset < sizeof(list)) {
            (void)snprintf(list + offset, sizeof(list) - offset, " …");
        }

        ESP_LOGW(TAG,
                 "I2C scan (0x08-0x77): %zu périphérique(s) répondent (%s).",
                 found, list[0] ? list : "-");
        ESP_LOGW(TAG,
                 "Niveaux du bus après scan: SDA=%d SCL=%d (0=bas, 1=haut).",
                 sda_level, scl_level);

        if (!has_ch422g_candidate) {
            ESP_LOGW(TAG,
                     "Aucun accusé de réception sur la plage CH422G 0x%02X–0x%02X.",
                     CH422G_SCAN_MIN_ADDR, CH422G_SCAN_MAX_ADDR);
        }

        if (has_gt911) {
            ESP_LOGW(TAG,
                     "Le contrôleur tactile GT911 reste visible (0x%02X/0x%02X) : le bus est actif, la panne vise l'extenseur.",
                     GT911_ADDR_PRIMARY, GT911_ADDR_BACKUP);
        }

        if (found > limit) {
            ESP_LOGW(TAG,
                     "Liste tronquée aux %zu premières adresses sur %zu détectées.",
                     limit, found);
        }
        return;
    }

    ESP_LOGW(TAG, "I2C scan diagnostic impossible: %s", esp_err_to_name(scan_ret));
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

esp_err_t ch422g_pin_mode(uint8_t exio_index, ch422g_pin_mode_t mode)
{
    ESP_RETURN_ON_FALSE(exio_index >= 1u && exio_index <= 8u, ESP_ERR_INVALID_ARG, TAG,
                        "invalid EXIO%u", (unsigned)exio_index);

    if (mode != CH422G_PIN_MODE_OUTPUT) {
        if (!s_input_mode_warned) {
            ESP_LOGW(TAG,
                     "EXIO%u requested in input mode but the current driver only supports "
                     "push-pull outputs. Ignoring request.",
                     (unsigned)exio_index);
            s_input_mode_warned = true;
        }
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ch422g_init();
}

uint8_t ch422g_exio_shadow_get(void)
{
    return s_shadow;
}

uint8_t ch422g_get_address(void)
{
    return s_addr;
}

esp_err_t ch422g_init(void)
{
    if (s_dev != NULL) {
        return ESP_OK;
    }

    DEV_I2C_Port port = DEV_I2C_Init();
    ESP_RETURN_ON_FALSE(port.bus != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C bus unavailable");

    uint8_t detected_addr = CH422G_I2C_ADDR_DEFAULT;
    esp_err_t ret = ch422g_probe_address(CH422G_I2C_ADDR_DEFAULT);
    if (ret == ESP_OK) {
        detected_addr = CH422G_I2C_ADDR_DEFAULT;
    } else {
        ret = ch422g_scan(CH422G_SCAN_MIN_ADDR, CH422G_SCAN_MAX_ADDR, &detected_addr);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "No ACK from CH422G between 0x%02X and 0x%02X (configured 0x%02X): %s. "
                 "Check 3V3 supply, SDA=%d, SCL=%d and external pull-ups (2.2k–4.7kΩ).",
                 CH422G_SCAN_MIN_ADDR, CH422G_SCAN_MAX_ADDR, CH422G_I2C_ADDR_DEFAULT,
                 esp_err_to_name(ret), CONFIG_I2C_MASTER_SDA_GPIO, CONFIG_I2C_MASTER_SCL_GPIO);
        if (!s_diag_logged) {
            ch422g_log_bus_snapshot();
            s_diag_logged = true;
        }
        return ret;
    }

    s_addr = detected_addr;

    if (s_addr != CH422G_I2C_ADDR_DEFAULT) {
        ESP_LOGW(TAG,
                 "CH422G responded on 0x%02X instead of configured 0x%02X. Verify A0/A1 straps "
                 "or update CONFIG_CH422G_I2C_ADDR.",
                 s_addr, CH422G_I2C_ADDR_DEFAULT);
    }

    ret = DEV_I2C_Set_Slave_Addr(&s_dev, s_addr);
    ESP_RETURN_ON_ERROR(ret, TAG, "attach CH422G");

    /* Force all EXIO outputs high so that downstream peripherals stay
     * deselected until explicitly toggled. */
    s_shadow = 0xFFu;
    ret = ch422g_write_shadow();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CH422G prêt @0x%02X (SDA=%d SCL=%d)", s_addr, CONFIG_I2C_MASTER_SDA_GPIO, CONFIG_I2C_MASTER_SCL_GPIO);
        s_diag_logged = false;
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

    esp_err_t update_ret = ch422g_write_shadow();
    if (update_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update EXIO%u: %s", (unsigned)exio_index, esp_err_to_name(update_ret));
    }
    return update_ret;
}
