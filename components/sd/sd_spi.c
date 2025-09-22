#include "sd.h"

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";

static sdmmc_card_t *s_card = NULL;
static bool s_spi_bus_ready = false;

static esp_err_t sd_spi_bus_acquire(void)
{
    if (s_spi_bus_ready) {
        return ESP_OK;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = CONFIG_SD_SPI_MOSI_IO,
        .miso_io_num = CONFIG_SD_SPI_MISO_IO,
        .sclk_io_num = CONFIG_SD_SPI_SCLK_IO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err == ESP_ERR_INVALID_STATE) {
        s_spi_bus_ready = true;
        return ESP_OK;
    }

    if (err == ESP_OK) {
        s_spi_bus_ready = true;
    }

    return err;
}

static void sd_spi_bus_release(void)
{
    if (!s_spi_bus_ready) {
        return;
    }

    esp_err_t err = spi_bus_free(SPI2_HOST);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "spi_bus_free: %s", esp_err_to_name(err));
    }

    s_spi_bus_ready = false;
}

static void sd_prepare_cs_gpio(void)
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
        ESP_LOGW(TAG, "gpio_config(CS=%d): %s", CONFIG_SD_FALLBACK_CS_GPIO, esp_err_to_name(err));
        return;
    }
    gpio_set_level(CONFIG_SD_FALLBACK_CS_GPIO, 1);
}

esp_err_t sd_mount(sdmmc_card_t **out_card)
{
    if (s_card) {
        if (out_card) {
            *out_card = s_card;
        }
        ESP_LOGW(TAG, "Déjà montée");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(sd_spi_bus_acquire(), TAG, "spi_bus_initialize");

    sd_prepare_cs_gpio();

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 400; // initialisation lente et robuste

#if CONFIG_SD_USE_FALLBACK_GPIO_CS
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = host.slot;
    slot_config.gpio_cs = CONFIG_SD_FALLBACK_CS_GPIO;
    slot_config.gpio_miso = CONFIG_SD_SPI_MISO_IO;
    slot_config.gpio_mosi = CONFIG_SD_SPI_MOSI_IO;
    slot_config.gpio_sclk = CONFIG_SD_SPI_SCLK_IO;
#else
    #error "Interdit: CS via CH422G non supporté par SDSPI (callbacks ISR)."
#endif

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "Bus SPI2 MOSI=%d MISO=%d SCLK=%d CS=%d",
             CONFIG_SD_SPI_MOSI_IO,
             CONFIG_SD_SPI_MISO_IO,
             CONFIG_SD_SPI_SCLK_IO,
             CONFIG_SD_FALLBACK_CS_GPIO);

    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdspi_mount: %s", esp_err_to_name(ret));
        s_card = NULL;
        sd_spi_bus_release();
        return ret;
    }

#ifndef SD_OCR_SDHC_CAP
#define SD_OCR_SDHC_CAP (1U << 30)
#endif
    const char *type_str = (s_card->ocr & SD_OCR_SDHC_CAP) ? "SDHC/SDXC" : "SDSC";
    ESP_LOGI(TAG, "Carte SD: %s, CSD/CID OK", type_str);

    if (out_card) {
        *out_card = s_card;
    }

    return ESP_OK;
}

esp_err_t sd_unmount(void)
{
    if (!s_card) {
        return ESP_OK;
    }

    esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_vfs_fat_sdcard_unmount: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "SD démontée");
    }

    s_card = NULL;
    sd_spi_bus_release();
    return err;
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
    sd_prepare_cs_gpio();
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
