#include "sd.h"

#include "sdkconfig.h"
#include "ch422g.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_vfs_fat.h"

#define TAG "sd"

#define SD_SPI_HOST SPI2_HOST
#define SD_SPI_DMA_CHANNEL SPI_DMA_CH_AUTO
#define SD_SPI_MAX_TRANSFER (4 * 1024)
#define SD_SPI_INIT_FREQ_KHZ 400

#define SD_SPI_MOSI 11
#define SD_SPI_MISO 13
#define SD_SPI_SCLK 12

#ifndef CONFIG_CH422G_EXIO_SD_CS
#define CONFIG_CH422G_EXIO_SD_CS 4
#endif

#define CH422G_EXIO_SD_CS CONFIG_CH422G_EXIO_SD_CS

#ifndef CONFIG_STORAGE_SD_USE_GPIO_CS
#define CONFIG_STORAGE_SD_USE_GPIO_CS 0
#endif

#if CONFIG_STORAGE_SD_USE_GPIO_CS
#define STORAGE_SD_GPIO_CS CONFIG_STORAGE_SD_GPIO_CS_NUM
#endif

static sdmmc_card_t *s_card = NULL;
static bool s_bus_ready = false;

#if CONFIG_STORAGE_SD_USE_GPIO_CS
static bool s_direct_cs_configured = false;
#endif

static esp_err_t sd_bus_ensure(void)
{
    if (s_bus_ready) {
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
        .max_transfer_sz = SD_SPI_MAX_TRANSFER,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
    };

    esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &buscfg, SD_SPI_DMA_CHANNEL);
    if (err == ESP_ERR_INVALID_STATE) {
        s_bus_ready = true;
        return ESP_OK;
    }
    if (err == ESP_OK) {
        s_bus_ready = true;
    }
    return err;
}

#if CONFIG_STORAGE_SD_USE_GPIO_CS
static esp_err_t sd_configure_direct_cs(void)
{
    if (s_direct_cs_configured) {
        return ESP_OK;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << STORAGE_SD_GPIO_CS,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio_config CS");
    ESP_RETURN_ON_ERROR(gpio_set_level(STORAGE_SD_GPIO_CS, 1), TAG, "gpio high");
    ESP_LOGI(TAG, "Direct SD CS fallback active on GPIO%d", STORAGE_SD_GPIO_CS);
    s_direct_cs_configured = true;
    return ESP_OK;
}
#else
static inline esp_err_t sd_ch422g_select(void)
{
    return ch422g_exio_set(CH422G_EXIO_SD_CS, false);
}

static inline esp_err_t sd_ch422g_deselect(void)
{
    return ch422g_exio_set(CH422G_EXIO_SD_CS, true);
}

static esp_err_t sdspi_ch422g_do_transaction(sdspi_dev_handle_t handle, sdmmc_command_t *cmdinfo)
{
    esp_err_t err = sd_ch422g_select();
    if (err != ESP_OK) {
        return err;
    }

    err = sdspi_host_do_transaction(handle, cmdinfo);

    esp_err_t release_err = sd_ch422g_deselect();
    if (release_err != ESP_OK && err == ESP_OK) {
        err = release_err;
    }

    return err;
}
#endif

bool sd_is_mounted(void)
{
    return s_card != NULL;
}

esp_err_t sd_mount(sdmmc_card_t **out_card)
{
    if (s_card != NULL) {
        if (out_card) {
            *out_card = s_card;
        }
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(sd_bus_ensure(), TAG, "spi_bus_initialize");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = SD_SPI_INIT_FREQ_KHZ;
#if !CONFIG_STORAGE_SD_USE_GPIO_CS
    host.do_transaction = sdspi_ch422g_do_transaction;
#endif

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.host_id = host.slot;
#if CONFIG_STORAGE_SD_USE_GPIO_CS
    ESP_RETURN_ON_ERROR(sd_configure_direct_cs(), TAG, "direct CS setup");
    ESP_RETURN_ON_ERROR(gpio_set_level(STORAGE_SD_GPIO_CS, 1), TAG, "CS idle high");
    slot_cfg.gpio_cs = STORAGE_SD_GPIO_CS;
#else
    ESP_RETURN_ON_ERROR(ch422g_init(), TAG, "ch422g init");
    ESP_RETURN_ON_ERROR(sd_ch422g_deselect(), TAG, "CS idle high");
    slot_cfg.gpio_cs = SDSPI_SLOT_NO_CS;
#endif

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

#if CONFIG_STORAGE_SD_USE_GPIO_CS
    ESP_RETURN_ON_ERROR(gpio_set_level(STORAGE_SD_GPIO_CS, 1), TAG, "CS release");
#else
    ESP_RETURN_ON_ERROR(sd_ch422g_deselect(), TAG, "CS release");
#endif

    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdspi_mount failed: %s", esp_err_to_name(err));
        spi_bus_free(SD_SPI_HOST);
        s_bus_ready = false;
        s_card = NULL;
        return err;
    }

    sdmmc_card_print_info(stdout, s_card);

#if CONFIG_STORAGE_SD_USE_GPIO_CS
    gpio_set_level(STORAGE_SD_GPIO_CS, 1);
#else
    sd_ch422g_deselect();
#endif

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
        ESP_LOGW(TAG, "esp_vfs_fat_sdcard_unmount failed: %s", esp_err_to_name(err));
    }
    s_card = NULL;

#if CONFIG_STORAGE_SD_USE_GPIO_CS
    if (s_direct_cs_configured) {
        gpio_set_level(STORAGE_SD_GPIO_CS, 1);
    }
#else
    sd_ch422g_deselect();
#endif

    if (s_bus_ready) {
        spi_bus_free(SD_SPI_HOST);
        s_bus_ready = false;
    }

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
#if CONFIG_STORAGE_SD_USE_GPIO_CS
    ESP_RETURN_ON_ERROR(sd_configure_direct_cs(), TAG, "direct CS");
    ESP_RETURN_ON_ERROR(gpio_set_level(STORAGE_SD_GPIO_CS, 0), TAG, "CS low");
    esp_rom_delay_us(5);
    ESP_RETURN_ON_ERROR(gpio_set_level(STORAGE_SD_GPIO_CS, 1), TAG, "CS high");
#else
    ESP_RETURN_ON_ERROR(ch422g_init(), TAG, "ch422g init");
    ESP_RETURN_ON_ERROR(sd_ch422g_select(), TAG, "CS low");
    esp_rom_delay_us(5);
    ESP_RETURN_ON_ERROR(sd_ch422g_deselect(), TAG, "CS high");
#endif
    return ESP_OK;
}
