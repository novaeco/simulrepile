#include "sd.h"

#include <inttypes.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define TAG "SD"

#define SD_SPI_HOST SPI2_HOST
#define SD_SPI_MOSI 11
#define SD_SPI_MISO 13
#define SD_SPI_SCLK 12
#define SD_SPI_DMA_CHANNEL SPI_DMA_CH_AUTO
#define SD_SPI_INIT_FREQ_KHZ 400
#define SD_SPI_FAST_FREQ_KHZ 20000

#if CONFIG_SD_USE_FALLBACK_GPIO_CS
#define SD_SPI_CS_GPIO CONFIG_SD_FALLBACK_CS_GPIO
#else
#define SD_SPI_CS_GPIO 34
#endif

static sdmmc_card_t *s_card = NULL;
static bool s_spi_bus_ready = false;

static esp_err_t sd_spi_bus_ensure(void)
{
    if (s_spi_bus_ready) {
        return ESP_OK;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = SD_SPI_MOSI,
        .miso_io_num = SD_SPI_MISO,
        .sclk_io_num = SD_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = 4096,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
    };

    esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &buscfg, SD_SPI_DMA_CHANNEL);
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
    esp_err_t err = spi_bus_free(SD_SPI_HOST);
    if (err == ESP_OK) {
        s_spi_bus_ready = false;
    } else if (err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SD: libération bus SPI échouée (%s)", esp_err_to_name(err));
    }
}

static void sd_gpio_prepare(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << SD_SPI_CS_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD: configuration GPIO%u échouée (%s)", SD_SPI_CS_GPIO, esp_err_to_name(err));
    }
    gpio_set_level(SD_SPI_CS_GPIO, 1);
}

esp_err_t sd_mount(sdmmc_card_t **out_card)
{
    if (s_card != NULL) {
        if (out_card) {
            *out_card = s_card;
        }
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(sd_spi_bus_ensure(), TAG, "spi_bus_initialize");
    sd_gpio_prepare();

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = SD_SPI_INIT_FREQ_KHZ;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = host.slot;
    slot_config.gpio_cs = SD_SPI_CS_GPIO;

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    ESP_LOGI(TAG, "SD: bus SPI2 MOSI=%d MISO=%d SCLK=%d CS=%d", SD_SPI_MOSI, SD_SPI_MISO, SD_SPI_SCLK, SD_SPI_CS_GPIO);

    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD: montage échoué (%s)", esp_err_to_name(err));
        gpio_set_level(SD_SPI_CS_GPIO, 1);
        sd_spi_bus_release();
        return err;
    }

    const char *type_str = (s_card->ocr & SDMMC_OCR_SDHC_CAPACITY) ? "SDHC/SDXC" : "SDSC";
    ESP_LOGI(TAG, "SD: carte montée %s (type=%s, CSD/CID ok)", SD_MOUNT_POINT, type_str);
    sdmmc_card_print_info(stdout, s_card);

    if (host.set_card_clk) {
        esp_err_t clk_err = host.set_card_clk(host.slot, SD_SPI_FAST_FREQ_KHZ);
        if (clk_err == ESP_OK) {
            ESP_LOGI(TAG, "SD: fréquence SPI réglée à %d kHz", SD_SPI_FAST_FREQ_KHZ);
        } else {
            ESP_LOGW(TAG, "SD: impossible de régler la fréquence SPI à %d kHz (%s)", SD_SPI_FAST_FREQ_KHZ, esp_err_to_name(clk_err));
        }
    }

    if (out_card) {
        *out_card = s_card;
    }

    return ESP_OK;
}

esp_err_t sd_unmount(void)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD: démontage partiel (%s)", esp_err_to_name(err));
    }
    s_card = NULL;
    gpio_set_level(SD_SPI_CS_GPIO, 1);
    sd_spi_bus_release();
    return err;
}

esp_err_t sd_card_print_info_stream(FILE *stream)
{
    if (s_card == NULL) {
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
    sd_gpio_prepare();
    esp_rom_delay_us(5);
    gpio_set_level(SD_SPI_CS_GPIO, 0);
    esp_rom_delay_us(5);
    gpio_set_level(SD_SPI_CS_GPIO, 1);
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
    return SD_SPI_CS_GPIO;
}
