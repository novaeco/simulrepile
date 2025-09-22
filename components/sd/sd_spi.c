#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"

// En IDF 5.5, le header SDSPI peut être "driver/sdspi_host.h" ou "esp_driver_sdspi.h"
#if __has_include("driver/sdspi_host.h")
  #include "driver/sdspi_host.h"
#else
  #include "esp_driver_sdspi.h"
#endif

#include "driver/gpio.h"
#include "esp_rom_sys.h"

static const char *TAG = "sd";
static sdmmc_card_t *s_card = NULL;

esp_err_t sd_mount(void)
{
    if (s_card) {
        ESP_LOGW(TAG, "Déjà montée");
        return ESP_OK;
    }

    // Hôte SDSPI (SPI2/VSPI)
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 400;  // init lente et robuste (augmentable après validation)

    // Broches SPI: se déclarent ici (PAS dans sdspi_device_config_t)
    spi_bus_config_t buscfg = {
        .mosi_io_num = CONFIG_SD_SPI_MOSI_IO, // 11
        .miso_io_num = CONFIG_SD_SPI_MISO_IO, // 13
        .sclk_io_num = CONFIG_SD_SPI_SCLK_IO, // 12
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_RETURN_ON_ERROR(
        spi_bus_initialize((spi_host_device_t)host.slot, &buscfg, SPI_DMA_CH_AUTO),
        TAG, "spi_bus_initialize"
    );

    // Slot/device SDSPI : seulement CS + host_id en IDF 5.5
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_SD_FALLBACK_CS_GPIO;           // 34 (GPIO natif)
    slot_config.host_id = (spi_host_device_t)host.slot;         // associer l'hôte

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "Bus SPI2 MOSI=%d MISO=%d SCLK=%d CS=%d",
             CONFIG_SD_SPI_MOSI_IO, CONFIG_SD_SPI_MISO_IO,
             CONFIG_SD_SPI_SCLK_IO, CONFIG_SD_FALLBACK_CS_GPIO);

    esp_err_t ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdspi_mount: %s", esp_err_to_name(ret));
        spi_bus_free((spi_host_device_t)host.slot);
        return ret;
    }

#ifndef SD_OCR_SDHC_CAP
#define SD_OCR_SDHC_CAP (1U << 30) // bit 30 (High Capacity: SDHC/SDXC)
#endif
    const char *type_str = (s_card->ocr & SD_OCR_SDHC_CAP) ? "SDHC/SDXC" : "SDSC";
    ESP_LOGI(TAG, "Carte SD: %s, CSD/CID OK", type_str);

    return ESP_OK;
}

esp_err_t sd_unmount(void)
{
    if (!s_card) return ESP_OK;
    ESP_ERROR_CHECK(esp_vfs_fat_sdcard_unmount("/sdcard", s_card));
    s_card = NULL;
    // SDSPI_HOST_DEFAULT() utilise SPI2_HOST par défaut sur S3
    spi_bus_free(SPI2_HOST);
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
        .pin_bit_mask = 1ULL << CONFIG_SD_FALLBACK_CS_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Config GPIO CS%d échouée: %s", CONFIG_SD_FALLBACK_CS_GPIO, esp_err_to_name(err));
        return err;
    }

    gpio_set_level(CONFIG_SD_FALLBACK_CS_GPIO, 1);
    esp_rom_delay_us(5);
    gpio_set_level(CONFIG_SD_FALLBACK_CS_GPIO, 0);
    esp_rom_delay_us(5);
    gpio_set_level(CONFIG_SD_FALLBACK_CS_GPIO, 1);
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
    return CONFIG_SD_FALLBACK_CS_GPIO;
}
