#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sd.h"

#ifndef CONFIG_SD_SPI_MAX_FREQ_KHZ
#define CONFIG_SD_SPI_MAX_FREQ_KHZ 20000
#endif

#ifndef CONFIG_SD_SPI_RETRY_FREQ_KHZ
#define CONFIG_SD_SPI_RETRY_FREQ_KHZ 12000
#endif

static const char *TAG = "sd";
static sdmmc_card_t *s_card = NULL;
static bool s_spi_bus_owned = false;

static inline uint32_t sdspi_select_frequency(int attempt)
{
    uint32_t primary = CONFIG_SD_SPI_MAX_FREQ_KHZ;
    uint32_t fallback = CONFIG_SD_SPI_RETRY_FREQ_KHZ;

    if (primary == 0) {
        primary = 20000;
    }
    if (fallback == 0) {
        fallback = primary;
    }
    if (fallback > primary) {
        fallback = primary;
    }

    return (attempt == 0) ? primary : fallback;
}

static inline spi_bus_config_t sdspi_bus_config(void) {
    spi_bus_config_t cfg = {
        .mosi_io_num = CONFIG_SD_SPI_MOSI_IO,
        .miso_io_num = CONFIG_SD_SPI_MISO_IO,
        .sclk_io_num = CONFIG_SD_SPI_SCLK_IO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4 * 1024,
    };
    return cfg;
}

esp_err_t sd_mount(void)
{
    if (s_card) {
        ESP_LOGW(TAG, "Déjà montée");
        return ESP_OK;
    }

    const char *mount_point = SD_MOUNT_POINT;
    const spi_host_device_t spi_host = SPI3_HOST;
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG,
             "SDSPI host=SPI3 MOSI=%d MISO=%d SCLK=%d CS=%d",
             CONFIG_SD_SPI_MOSI_IO,
             CONFIG_SD_SPI_MISO_IO,
             CONFIG_SD_SPI_SCLK_IO,
             CONFIG_SD_SPI_CS_IO);

    s_spi_bus_owned = false;

    for (int attempt = 0; attempt < 2; ++attempt) {
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = spi_host;
        host.max_freq_khz = sdspi_select_frequency(attempt);

        spi_bus_config_t bus_cfg = sdspi_bus_config();
        bool bus_owned = false;
        esp_err_t err = spi_bus_initialize(spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
        if (err == ESP_OK) {
            bus_owned = true;
        } else if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "SPI%d déjà initialisé, tentative %d", spi_host + 1, attempt + 1);
        } else {
            ESP_LOGE(TAG, "spi_bus_initialize(SPI%d) a échoué: %s", spi_host + 1, esp_err_to_name(err));
            return err;
        }

        sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_cfg.gpio_cs = CONFIG_SD_SPI_CS_IO;
        slot_cfg.host_id = spi_host;

        ESP_LOGI(TAG,
                 "Tentative %d: fréquence SDSPI %d kHz (point de montage %s)",
                 attempt + 1,
                 host.max_freq_khz,
                 mount_point);

        esp_err_t ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_cfg, &mount_cfg, &s_card);
        if (ret == ESP_OK) {
            s_spi_bus_owned = bus_owned;
            const bool is_sdhc = (s_card->ocr & (1u << 30)) != 0;
            ESP_LOGI(TAG, "Carte détectée: %s", is_sdhc ? "SDHC/SDXC" : "SDSC");
            return ESP_OK;
        }

        ESP_LOGE(TAG,
                 "Montage SDSPI échoué (tentative %d/2): %s",
                 attempt + 1,
                 esp_err_to_name(ret));

        if (s_card) {
            esp_vfs_fat_sdcard_unmount(mount_point, s_card);
            s_card = NULL;
        }

        if (bus_owned) {
            esp_err_t free_ret = spi_bus_free(spi_host);
            s_spi_bus_owned = false;
            if (free_ret != ESP_OK) {
                ESP_LOGW(TAG, "spi_bus_free(SPI%d) a échoué: %s", spi_host + 1, esp_err_to_name(free_ret));
            }
        }

        if ((ret == ESP_ERR_TIMEOUT || ret == ESP_FAIL) && attempt == 0) {
            ESP_LOGW(TAG, "Nouvelle tentative SDSPI dans 200 ms à 12 MHz");
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        return ret;
    }

    return ESP_FAIL;
}

esp_err_t sd_unmount(void)
{
    if (!s_card) {
        return ESP_OK;
    }

    const char *mount_point = SD_MOUNT_POINT;
    ESP_ERROR_CHECK(esp_vfs_fat_sdcard_unmount(mount_point, s_card));
    s_card = NULL;

    if (s_spi_bus_owned) {
        ESP_ERROR_CHECK(spi_bus_free(SPI3_HOST));
        s_spi_bus_owned = false;
    }

    ESP_LOGI(TAG, "SD démontée");
    return ESP_OK;
}

sdmmc_card_t *sd_get_card(void)
{
    return s_card;
}

esp_err_t sd_card_print_info_stream(FILE *stream)
{
    if (!s_card) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!stream) {
        stream = stdout;
    }

    sdmmc_card_print_info(stream, s_card);
    return ESP_OK;
}

esp_err_t sd_card_print_info(void)
{
    return sd_card_print_info_stream(stdout);
}

esp_err_t sd_spi_cs_selftest(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_SD_SPI_CS_IO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Config GPIO CS%d échouée: %s", CONFIG_SD_SPI_CS_IO, esp_err_to_name(err));
        return err;
    }

    gpio_set_level(CONFIG_SD_SPI_CS_IO, 1);
    esp_rom_delay_us(5);
    gpio_set_level(CONFIG_SD_SPI_CS_IO, 0);
    esp_rom_delay_us(5);
    gpio_set_level(CONFIG_SD_SPI_CS_IO, 1);
    return ESP_OK;
}

bool sd_is_mounted(void)
{
    return s_card != NULL;
}

bool sd_uses_direct_cs(void)
{
    return true;
}

int sd_get_cs_gpio(void)
{
    return CONFIG_SD_SPI_CS_IO;
}
