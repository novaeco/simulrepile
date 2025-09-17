/*****************************************************************************
 * | File         :   sd.c
 * | Author       :   Waveshare team
 * | Function     :   SD card driver code for mounting, reading capacity, and unmounting
 * | Info         :
 * |                  This is the C file for SD card configuration and usage.
 * ----------------
 * | This version :   V1.0
 * | Date         :   2024-11-28
 * | Info         :   Basic version, includes functions to initialize,
 * |                  read memory capacity, and manage SD card mounting/unmounting.
 *
 ******************************************************************************/

#include <stdbool.h>

#include "esp_log.h"
#include "sd.h"  // Include header file for SD card functions
#include "driver/spi_common.h"

// Global variable for SD card structure
static sdmmc_card_t *card;
static sdspi_dev_handle_t sdspi_device = -1;
static bool spi_bus_initialized = false;

// Define the mount point for the SD card
const char mount_point[] = MOUNT_POINT;

/**
 * @brief Initialize the SD card and mount the filesystem.
 *
 * This function configures the SDSPI host, sets up the SPI bus and device,
 * and mounts the FAT filesystem from the SD card. The mounting is attempted
 * multiple times, and each attempt is logged for diagnostic purposes.
 *
 * @retval ESP_OK if initialization and mounting succeed.
 * @retval ESP_FAIL if an error occurs during the process after all attempts.
 */
esp_err_t sd_mmc_init() {
    if (card != NULL) {
        return ESP_OK;
    }

    sdspi_device = -1;
    esp_err_t ret = ESP_FAIL;
    const int max_attempts = 3;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = EXAMPLE_FORMAT_IF_MOUNT_FAILED,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    spi_bus_config_t bus_config = {
        .mosi_io_num = SD_SPI_MOSI,
        .miso_io_num = SD_SPI_MISO,
        .sclk_io_num = SD_SPI_CLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 16 * 1024,
    };

    if (!spi_bus_initialized) {
        esp_err_t bus_ret = spi_bus_initialize(SD_SPI_HOST, &bus_config, SDSPI_DEFAULT_DMA);
        if (bus_ret == ESP_OK || bus_ret == ESP_ERR_INVALID_STATE) {
            spi_bus_initialized = true;
        } else {
            ESP_LOGE(SD_TAG, "Failed to initialize SPI bus for SD card (%s)", esp_err_to_name(bus_ret));
            return bus_ret;
        }
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = SD_SPI_HOST;
    slot_config.gpio_cs = SD_SPI_CS;
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP;
    slot_config.gpio_int = SDSPI_SLOT_NO_INT;

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        ESP_LOGI(SD_TAG, "Initializing SD card over SPI (attempt %d/%d)", attempt, max_attempts);

        ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
        if (ret == ESP_OK) {
            sdspi_device = card->host.slot;
            ESP_LOGI(SD_TAG, "Filesystem mounted on attempt %d", attempt);
            return ESP_OK;
        }

        if (ret == ESP_FAIL) {
            ESP_LOGE(SD_TAG, "Failed to mount filesystem on attempt %d. Format the card if mount fails.", attempt);
        } else {
            ESP_LOGE(SD_TAG, "Failed to initialize the card on attempt %d (%s)",
                     attempt, esp_err_to_name(ret));
        }
    }

    ESP_LOGE(SD_TAG, "SD card initialization failed after %d attempts", max_attempts);
    return ret;
}

/**
 * @brief Print detailed SD card information to the console.
 * 
 * Uses the built-in `sdmmc_card_print_info` function to log information 
 * about the SD card to the standard output.
 */
esp_err_t sd_card_print_info() {
    if (card == NULL || sdspi_device < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}

/**
 * @brief Unmount the SD card and release resources.
 * 
 * This function unmounts the FAT filesystem and ensures all resources 
 * associated with the SD card are released.
 * 
 * @retval ESP_OK if unmounting succeeds.
 * @retval ESP_FAIL if an error occurs.
 */
esp_err_t sd_mmc_unmount() {
    if (card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    sdspi_dev_handle_t handle = sdspi_device;
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, card);
    card = NULL;
    sdspi_device = -1;

    if (ret != ESP_OK) {
        ESP_LOGE(SD_TAG, "Failed to unmount SD card: %s", esp_err_to_name(ret));
        if (handle >= 0) {
            // Best-effort cleanup when the high-level unmount fails.
            (void)sdspi_host_remove_device(handle);
        }
    }

    if (spi_bus_initialized) {
        esp_err_t bus_ret = spi_bus_free(SD_SPI_HOST);
        if (bus_ret != ESP_OK && bus_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(SD_TAG, "Failed to free SPI bus: %s", esp_err_to_name(bus_ret));
        }
        spi_bus_initialized = false;
    }

    return ret;
}

/**
 * @brief Get total and available memory capacity of the SD card.
 * 
 * @param total_capacity Pointer to store the total capacity (in KB).
 * @param available_capacity Pointer to store the available capacity (in KB).
 * 
 * @retval ESP_OK if memory information is successfully retrieved.
 * @retval ESP_FAIL if an error occurs while fetching capacity information.
 */
esp_err_t read_sd_capacity(size_t *total_capacity, size_t *available_capacity) {
    if (card == NULL || sdspi_device < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    FATFS *fs;
    uint32_t free_clusters;

    // Get the number of free clusters in the filesystem
    int res = f_getfree(mount_point, &free_clusters, &fs);
    if (res != FR_OK) {
        ESP_LOGE(SD_TAG, "Failed to get number of free clusters (%d)", res);
        return ESP_FAIL;
    }

    // Calculate total and free sectors based on cluster size
    uint64_t total_sectors = ((uint64_t)(fs->n_fatent - 2)) * fs->csize;
    uint64_t free_sectors = ((uint64_t)free_clusters) * fs->csize;

    // Convert sectors to size in KB
    size_t sd_total_KB = (total_sectors * fs->ssize) / 1024;
    size_t sd_available_KB = (free_sectors * fs->ssize) / 1024;

    // Store total capacity if the pointer is valid
    if (total_capacity != NULL) {
        *total_capacity = sd_total_KB;
    }

    // Store available capacity if the pointer is valid
    if (available_capacity != NULL) {
        *available_capacity = sd_available_KB;
    }

    return ESP_OK;
}

