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

#include "sd.h"  // Include header file for SD card functions

// Global variable for SD card structure
static sdmmc_card_t *card;

// Define the mount point for the SD card
const char mount_point[] = MOUNT_POINT;

/**
 * @brief Initialize the SD card and mount the filesystem.
 *
 * This function configures the SDMMC peripheral, sets up the host and slot,
 * and mounts the FAT filesystem from the SD card. The mounting is attempted
 * multiple times, and each attempt is logged for diagnostic purposes.
 *
 * @retval ESP_OK if initialization and mounting succeed.
 * @retval ESP_FAIL if an error occurs during the process after all attempts.
 */
esp_err_t sd_mmc_init() {
    esp_err_t ret;
    const int max_attempts = 3;

    // Configuration for mounting the FAT filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = EXAMPLE_FORMAT_IF_MOUNT_FAILED, // Format if mount fails
        .max_files = 5,                  // Max number of open files
        .allocation_unit_size = 16 * 1024 // Allocation unit size
    };

    // Attempt to mount the filesystem multiple times
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        ESP_LOGI(SD_TAG, "Initializing SD card (attempt %d/%d)", attempt, max_attempts);

        // Use the SDMMC peripheral for SD card communication
        ESP_LOGI(SD_TAG, "Using SDMMC peripheral");

        // Host configuration with default settings for standard-speed operation
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = SDMMC_FREQ_PROBING;   // Start at low freq for marginal cards; increase after successful mount

        // Slot configuration for SDMMC
        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 1;
        slot_config.clk   = EXAMPLE_PIN_CLK;
        slot_config.cmd   = EXAMPLE_PIN_CMD;
        slot_config.d0    = EXAMPLE_PIN_D0;
        // Enable internal pull-ups on the GPIOs
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        ESP_LOGI(SD_TAG, "Mounting filesystem");

        // Mount the filesystem and initialize the SD card
        ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

        if (ret == ESP_OK) {
            ESP_LOGI(SD_TAG, "Filesystem mounted on attempt %d", attempt);
            return ESP_OK;
        }

        if (ret == ESP_FAIL) {
            ESP_LOGE(SD_TAG, "Failed to mount filesystem on attempt %d. Format the card if mount fails.", attempt);
        } else {
            ESP_LOGE(SD_TAG, "Failed to initialize the card on attempt %d (%s). Check pull-up resistors on the card lines.",
                     attempt, esp_err_to_name(ret));
        }

        if (card) {
            esp_vfs_fat_sdcard_unmount(mount_point, card);
            card = NULL;
        } else {
            sdmmc_host_deinit();
        }
    }

    ESP_LOGE(SD_TAG, "SD card initialization failed after %d attempts", max_attempts);
    if (card) {
        esp_vfs_fat_sdcard_unmount(mount_point, card);
        card = NULL;
    } else {
        sdmmc_host_deinit();
    }
    return ESP_FAIL;
}

/**
 * @brief Print detailed SD card information to the console.
 * 
 * Uses the built-in `sdmmc_card_print_info` function to log information 
 * about the SD card to the standard output.
 */
esp_err_t sd_card_print_info() {
    if (card == NULL) {
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
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, card);
    card = NULL;
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

