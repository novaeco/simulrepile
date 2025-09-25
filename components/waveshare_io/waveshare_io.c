#include "waveshare_io.h"

#include <limits.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

#include "ch422g.h"
#include "io_extension.h"
#include "i2c.h"
#include "sdkconfig.h"

#ifndef CONFIG_CH422G_EXIO_SD_CS
#define CONFIG_CH422G_EXIO_SD_CS 4
#endif

#define TAG "waveshare_io"

static waveshare_io_variant_t s_variant = WAVESHARE_IO_VARIANT_UNKNOWN;
static esp_err_t s_init_status = ESP_ERR_INVALID_STATE;
static bool s_ready = false;
static uint8_t s_ch422g_addr = 0;

static esp_err_t detect_ch32v003(void)
{
    DEV_I2C_Port port = DEV_I2C_Init();
    if (port.bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = DEV_I2C_Probe(IO_EXTENSION_ADDR);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = IO_EXTENSION_Init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "Waveshare IO extension (CH32V003) detected on 0x%02X (SDA=%d SCL=%d)",
                 IO_EXTENSION_ADDR,
                 CONFIG_I2C_MASTER_SDA_GPIO,
                 CONFIG_I2C_MASTER_SCL_GPIO);
    }
    return ret;
}

static esp_err_t detect_ch422g(void)
{
    esp_err_t ret = ch422g_init();
    if (ret == ESP_OK) {
        s_ch422g_addr = ch422g_get_address();
        ESP_LOGI(TAG,
                 "CH422G IO expander detected on 0x%02X (SDA=%d SCL=%d)",
                 s_ch422g_addr,
                 CONFIG_I2C_MASTER_SDA_GPIO,
                 CONFIG_I2C_MASTER_SCL_GPIO);
    }
    return ret;
}

esp_err_t waveshare_io_init(void)
{
    if (s_ready) {
        return s_init_status;
    }

    s_variant = WAVESHARE_IO_VARIANT_UNKNOWN;
    s_ch422g_addr = 0;

    esp_err_t ret = detect_ch32v003();
    if (ret == ESP_OK) {
        s_variant = WAVESHARE_IO_VARIANT_CH32V003;
        s_ready = true;
        s_init_status = ESP_OK;
        return ESP_OK;
    }

    if (ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "CH32V003 probe failed: %s", esp_err_to_name(ret));
    }

    ret = detect_ch422g();
    if (ret == ESP_OK) {
        s_variant = WAVESHARE_IO_VARIANT_CH422G;
        s_ready = true;
        s_init_status = ESP_OK;
        return ESP_OK;
    }

    s_ready = false;
    s_init_status = ret;
    ESP_LOGE(TAG,
             "No compatible IO expander detected (CH32V003 or CH422G). Last error: %s",
             esp_err_to_name(ret));
    return ret;
}

bool waveshare_io_ready(void)
{
    return s_ready && s_init_status == ESP_OK;
}

waveshare_io_variant_t waveshare_io_get_variant(void)
{
    return s_variant;
}

const char *waveshare_io_variant_name(waveshare_io_variant_t variant)
{
    switch (variant) {
    case WAVESHARE_IO_VARIANT_CH422G:
        return "CH422G";
    case WAVESHARE_IO_VARIANT_CH32V003:
        return "CH32V003";
    case WAVESHARE_IO_VARIANT_UNKNOWN:
    default:
        return "unknown";
    }
}

uint8_t waveshare_io_get_ch422g_address(void)
{
    return (s_variant == WAVESHARE_IO_VARIANT_CH422G) ? s_ch422g_addr : 0;
}

esp_err_t waveshare_io_output_set(uint8_t line, bool level_high)
{
    ESP_RETURN_ON_FALSE(waveshare_io_line_valid(line), ESP_ERR_INVALID_ARG, TAG,
                        "invalid IO line %u", line);

    esp_err_t err = waveshare_io_init();
    if (err != ESP_OK) {
        return err;
    }

    switch (s_variant) {
    case WAVESHARE_IO_VARIANT_CH422G:
        return ch422g_exio_set((uint8_t)(line + 1u), level_high);
    case WAVESHARE_IO_VARIANT_CH32V003:
        return IO_EXTENSION_Output(line, level_high ? 1 : 0);
    case WAVESHARE_IO_VARIANT_UNKNOWN:
    default:
        return s_init_status;
    }
}

esp_err_t waveshare_io_output_get(uint8_t line, bool *level_high)
{
    if (level_high == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_FALSE(waveshare_io_line_valid(line), ESP_ERR_INVALID_ARG, TAG,
                        "invalid IO line %u", line);

    esp_err_t err = waveshare_io_init();
    if (err != ESP_OK) {
        return err;
    }

    switch (s_variant) {
    case WAVESHARE_IO_VARIANT_CH422G: {
        uint8_t shadow = ch422g_exio_shadow_get();
        *level_high = (shadow & (1u << line)) != 0;
        return ESP_OK;
    }
    case WAVESHARE_IO_VARIANT_CH32V003: {
        uint8_t value = 0;
        err = IO_EXTENSION_Input(line, &value);
        if (err == ESP_OK) {
            *level_high = value != 0;
        }
        return err;
    }
    case WAVESHARE_IO_VARIANT_UNKNOWN:
    default:
        return s_init_status;
    }
}

static inline uint8_t sd_cs_line(void)
{
    return waveshare_io_line_from_exio(CONFIG_CH422G_EXIO_SD_CS);
}

esp_err_t waveshare_io_sd_select(void)
{
    uint8_t line = sd_cs_line();
    if (line == UINT8_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    return waveshare_io_output_set(line, false);
}

esp_err_t waveshare_io_sd_deselect(void)
{
    uint8_t line = sd_cs_line();
    if (line == UINT8_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    return waveshare_io_output_set(line, true);
}
