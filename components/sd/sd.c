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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_check.h"
#include "sd.h"  // Include header file for SD card functions
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"

// Global variable for SD card structure
static sdmmc_card_t *card;
static sdspi_dev_handle_t sdspi_device = -1;
static bool spi_bus_initialized = false;
static FATFS *sdcard_fs = NULL;
static BYTE sdcard_drive_num = FF_DRV_NOT_USED;
static char sdcard_drive_path[8] = {0};
static bool io_extension_ready = false;
static bool io_extension_init_error_logged = false;
static bool io_extension_drive_error_logged = false;

static esp_err_t sd_spi_ensure_io_extension(void);
static void sd_spi_drive_cs_level(int level);
static void sd_spi_assert_cs(spi_transaction_t *trans);
static void sd_spi_release_cs(spi_transaction_t *trans);
static esp_err_t sd_spi_do_transaction(int slot, sdmmc_command_t *cmd);
static size_t sd_spi_compute_allocation_unit(size_t sector_size, size_t requested_size);
static esp_err_t sd_spi_format_filesystem(const esp_vfs_fat_mount_config_t *mount_config, sdmmc_card_t *card);
static esp_err_t sd_spi_prepare_filesystem(const esp_vfs_fat_mount_config_t *mount_config, sdmmc_card_t *card);
static esp_err_t sd_spi_teardown_filesystem(void);
static esp_err_t sd_spi_attach_device(sdspi_dev_handle_t *out_handle);
static void sd_spi_detach_device(void);

// Define the mount point for the SD card
const char mount_point[] = MOUNT_POINT;

static esp_err_t sd_spi_ensure_io_extension(void) {
    if (io_extension_ready) {
        return ESP_OK;
    }

    esp_err_t ret = IO_EXTENSION_Init();
    if (ret == ESP_OK) {
        ret = IO_EXTENSION_Output(SD_SPI_CS, 1);
    }

    if (ret == ESP_OK) {
        io_extension_ready = true;
        io_extension_init_error_logged = false;
        io_extension_drive_error_logged = false;
        return ESP_OK;
    }

    if (!io_extension_init_error_logged) {
        ESP_LOGE(SD_TAG, "Failed to initialize IO extension for SD CS: %s", esp_err_to_name(ret));
        io_extension_init_error_logged = true;
    }
    return ret;
}

static void sd_spi_drive_cs_level(int level) {
    esp_err_t ret = sd_spi_ensure_io_extension();
    if (ret != ESP_OK) {
        return;
    }

    ret = IO_EXTENSION_Output(SD_SPI_CS, level ? 1 : 0);
    if (ret != ESP_OK) {
        if (!io_extension_drive_error_logged) {
            ESP_LOGE(SD_TAG, "Failed to drive SD CS line to %d: %s", level, esp_err_to_name(ret));
            io_extension_drive_error_logged = true;
        }
    } else {
        io_extension_drive_error_logged = false;
    }
}

static void sd_spi_assert_cs(spi_transaction_t *trans) {
    (void)trans;
    sd_spi_drive_cs_level(0);
}

static void sd_spi_release_cs(spi_transaction_t *trans) {
    (void)trans;
    sd_spi_drive_cs_level(1);
}

static esp_err_t sd_spi_do_transaction(int slot, sdmmc_command_t *cmd) {
    if (cmd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sdspi_dev_handle_t handle = (sdspi_dev_handle_t)slot;
    if (handle < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    sd_spi_assert_cs(NULL);
    esp_err_t ret = sdspi_host_do_transaction(handle, cmd);
    sd_spi_release_cs(NULL);
    return ret;
}

static size_t sd_spi_compute_allocation_unit(size_t sector_size, size_t requested_size) {
    const size_t max_sectors_per_cylinder = 128;
    size_t alloc_unit = requested_size;

    if (alloc_unit == 0) {
        alloc_unit = sector_size;
    }
    if (alloc_unit < sector_size) {
        alloc_unit = sector_size;
    }

    size_t max_size = sector_size * max_sectors_per_cylinder;
    if (alloc_unit > max_size) {
        alloc_unit = max_size;
    }

    return alloc_unit;
}

static esp_err_t sd_spi_format_filesystem(const esp_vfs_fat_mount_config_t *mount_config, sdmmc_card_t *card) {
    const size_t workbuf_size = 4096;
    void *workbuf = ff_memalloc(workbuf_size);
    if (workbuf == NULL) {
        ESP_LOGE(SD_TAG, "Failed to allocate work buffer for FAT formatting");
        return ESP_ERR_NO_MEM;
    }

    LBA_t plist[] = {100, 0, 0, 0};
    FRESULT res = f_fdisk(sdcard_drive_num, plist, workbuf);
    if (res != FR_OK) {
        ESP_LOGE(SD_TAG, "f_fdisk failed (%d)", res);
        ff_memfree(workbuf);
        return ESP_FAIL;
    }

    size_t alloc_unit = sd_spi_compute_allocation_unit(card->csd.sector_size, mount_config->allocation_unit_size);
    ESP_LOGW(SD_TAG, "Formatting card, allocation unit size=%zu", alloc_unit);
    MKFS_PARM opt = {
        .fmt = FM_ANY,
        .n_fat = mount_config->use_one_fat ? 1 : 2,
        .align = 0,
        .n_root = 0,
        .au_size = alloc_unit,
    };
    res = f_mkfs(sdcard_drive_path, &opt, workbuf, workbuf_size);
    ff_memfree(workbuf);
    if (res != FR_OK) {
        ESP_LOGE(SD_TAG, "f_mkfs failed (%d)", res);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t sd_spi_prepare_filesystem(const esp_vfs_fat_mount_config_t *mount_config, sdmmc_card_t *card) {
    if (sdcard_drive_num != FF_DRV_NOT_USED || sdcard_fs != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    BYTE pdrv = FF_DRV_NOT_USED;
    esp_err_t ret = ff_diskio_get_drive(&pdrv);
    if (ret != ESP_OK || pdrv == FF_DRV_NOT_USED) {
        ESP_LOGE(SD_TAG, "Unable to obtain FATFS drive number (%s)", ret == ESP_OK ? "no drive available" : esp_err_to_name(ret));
        return (ret == ESP_OK) ? ESP_ERR_NO_MEM : ret;
    }

    size_t written = snprintf(sdcard_drive_path, sizeof(sdcard_drive_path), "%u:", (unsigned)pdrv);
    ESP_RETURN_ON_FALSE(written < sizeof(sdcard_drive_path), ESP_ERR_INVALID_SIZE, SD_TAG,
                        "Drive path buffer too small for drive %u", (unsigned)pdrv);
    sdcard_drive_num = pdrv;

    ff_diskio_register_sdmmc(sdcard_drive_num, card);
    ff_sdmmc_set_disk_status_check(sdcard_drive_num, mount_config->disk_status_check_enable);

    esp_vfs_fat_conf_t conf = {
        .base_path = mount_point,
        .fat_drive = sdcard_drive_path,
        .max_files = mount_config->max_files,
    };

    esp_err_t reg_ret = esp_vfs_fat_register_cfg(&conf, &sdcard_fs);
    if (reg_ret != ESP_OK) {
        ESP_LOGE(SD_TAG, "Failed to register FATFS at %s (%s)", mount_point, esp_err_to_name(reg_ret));
        ff_diskio_unregister(sdcard_drive_num);
        sdcard_drive_num = FF_DRV_NOT_USED;
        sdcard_drive_path[0] = '\0';
        sdcard_fs = NULL;
        return reg_ret;
    }

    FRESULT res = f_mount(sdcard_fs, sdcard_drive_path, 1);
    if (res != FR_OK) {
        ESP_LOGW(SD_TAG, "Failed to mount filesystem (%d)", res);
        bool need_format = (res == FR_NO_FILESYSTEM || res == FR_INT_ERR) && mount_config->format_if_mount_failed;
        if (need_format) {
            ret = sd_spi_format_filesystem(mount_config, card);
            if (ret == ESP_OK) {
                res = f_mount(sdcard_fs, sdcard_drive_path, 1);
            }
        } else {
            ret = ESP_FAIL;
        }

        if (res != FR_OK || ret != ESP_OK) {
            if (ret == ESP_OK) {
                ret = ESP_FAIL;
            }
            f_mount(NULL, sdcard_drive_path, 0);
            esp_vfs_fat_unregister_path(mount_point);
            ff_diskio_unregister(sdcard_drive_num);
            sdcard_fs = NULL;
            sdcard_drive_num = FF_DRV_NOT_USED;
            sdcard_drive_path[0] = '\0';
            return ret;
        }
    }

    return ESP_OK;
}

static esp_err_t sd_spi_teardown_filesystem(void) {
    esp_err_t ret = ESP_OK;

    if (sdcard_drive_num != FF_DRV_NOT_USED && sdcard_drive_path[0] != '\0') {
        FRESULT res = f_mount(NULL, sdcard_drive_path, 0);
        if (res != FR_OK && ret == ESP_OK) {
            ESP_LOGW(SD_TAG, "Failed to unmount FAT filesystem (%d)", res);
            ret = ESP_FAIL;
        }
        ff_diskio_unregister(sdcard_drive_num);
        sdcard_drive_num = FF_DRV_NOT_USED;
        sdcard_drive_path[0] = '\0';
    }

    if (sdcard_fs != NULL) {
        esp_err_t unreg_ret = esp_vfs_fat_unregister_path(mount_point);
        if (unreg_ret != ESP_OK && ret == ESP_OK) {
            ESP_LOGW(SD_TAG, "Failed to unregister VFS path: %s", esp_err_to_name(unreg_ret));
            ret = unreg_ret;
        }
        sdcard_fs = NULL;
    }

    return ret;
}

static esp_err_t sd_spi_attach_device(sdspi_dev_handle_t *out_handle) {
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = SD_SPI_HOST;
    slot_config.gpio_cs = SDSPI_SLOT_NO_CS;
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP;
    slot_config.gpio_int = SDSPI_SLOT_NO_INT;

    esp_err_t ret = sdspi_host_init_device(&slot_config, out_handle);
    if (ret == ESP_OK) {
        sd_spi_release_cs(NULL);
    }
    return ret;
}

static void sd_spi_detach_device(void) {
    if (sdspi_device >= 0) {
        sdspi_dev_handle_t handle = sdspi_device;
        sdspi_device = -1;
        esp_err_t ret = sdspi_host_remove_device(handle);
        if (ret != ESP_OK) {
            ESP_LOGW(SD_TAG, "sdspi_host_remove_device failed: %s", esp_err_to_name(ret));
        }
    }

    esp_err_t deinit_ret = sdspi_host_deinit();
    if (deinit_ret != ESP_OK && deinit_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(SD_TAG, "sdspi_host_deinit failed: %s", esp_err_to_name(deinit_ret));
    }

    sd_spi_release_cs(NULL);
}

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

    const uint32_t sd_operating_freq_khz = SDMMC_FREQ_DEFAULT;

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        ESP_LOGI(SD_TAG, "Initializing SD card over SPI (attempt %d/%d)", attempt, max_attempts);

        sd_spi_release_cs(NULL);

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = SD_SPI_HOST;
        host.max_freq_khz = sd_operating_freq_khz;

        esp_err_t host_ret = host.init();
        if (host_ret != ESP_OK && host_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(SD_TAG, "Failed to initialize SDSPI host (%s)", esp_err_to_name(host_ret));
            ret = host_ret;
            break;
        }

        sdspi_dev_handle_t device_handle = -1;
        ret = sd_spi_attach_device(&device_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(SD_TAG, "Failed to attach SDSPI device (%s)", esp_err_to_name(ret));
            sdspi_device = -1;
            sd_spi_detach_device();
            continue;
        }

        sdspi_device = device_handle;
        host.slot = sdspi_device;
        host.max_freq_khz = sd_operating_freq_khz;
        host.do_transaction = sd_spi_do_transaction;

        card = (sdmmc_card_t *)calloc(1, sizeof(sdmmc_card_t));
        if (card == NULL) {
            ESP_LOGE(SD_TAG, "Failed to allocate sdmmc_card_t structure");
            ret = ESP_ERR_NO_MEM;
            sd_spi_teardown_filesystem();
            sd_spi_detach_device();
            break;
        }

        ret = sdmmc_card_init(&host, card);
        if (ret != ESP_OK) {
            ESP_LOGE(SD_TAG, "Failed to initialize SD card (%s)", esp_err_to_name(ret));
            free(card);
            card = NULL;
            sd_spi_teardown_filesystem();
            sd_spi_detach_device();
            continue;
        }

        esp_err_t clk_ret = sdspi_host_set_card_clk(sdspi_device, host.max_freq_khz);
        if (clk_ret != ESP_OK) {
            ESP_LOGE(SD_TAG, "Failed to set SD SPI clock (%s)", esp_err_to_name(clk_ret));
            free(card);
            card = NULL;
            sd_spi_teardown_filesystem();
            sd_spi_detach_device();
            ret = clk_ret;
            continue;
        }

        ret = sd_spi_prepare_filesystem(&mount_config, card);
        if (ret == ESP_OK) {
            ESP_LOGI(SD_TAG, "Filesystem mounted on attempt %d", attempt);
            return ESP_OK;
        }

        ESP_LOGE(SD_TAG, "Failed to mount filesystem on attempt %d (%s)", attempt, esp_err_to_name(ret));
        sd_spi_teardown_filesystem();
        free(card);
        card = NULL;
        sd_spi_detach_device();
    }

    ESP_LOGE(SD_TAG, "SD card initialization failed after %d attempts", max_attempts);
    sd_spi_teardown_filesystem();
    if (card != NULL) {
        free(card);
        card = NULL;
    }
    sd_spi_detach_device();
    sd_spi_release_cs(NULL);
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

    esp_err_t ret = sd_spi_teardown_filesystem();

    free(card);
    card = NULL;

    sd_spi_release_cs(NULL);
    sd_spi_detach_device();

    if (spi_bus_initialized) {
        esp_err_t bus_ret = spi_bus_free(SD_SPI_HOST);
        if (bus_ret != ESP_OK && bus_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(SD_TAG, "Failed to free SPI bus: %s", esp_err_to_name(bus_ret));
            if (ret == ESP_OK) {
                ret = bus_ret;
            }
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

