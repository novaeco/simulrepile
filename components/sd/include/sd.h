#pragma once

#include <stdbool.h>
#include <stdio.h>

#include "esp_err.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"

#ifndef CONFIG_SD_SPI_MOSI_IO
#define CONFIG_SD_SPI_MOSI_IO 11
#endif

#ifndef CONFIG_SD_SPI_MISO_IO
#define CONFIG_SD_SPI_MISO_IO 13
#endif

#ifndef CONFIG_SD_SPI_SCLK_IO
#define CONFIG_SD_SPI_SCLK_IO 12
#endif

#ifndef CONFIG_SD_SPI_CS_IO
#if defined(CONFIG_SD_FALLBACK_CS_GPIO)
#define CONFIG_SD_SPI_CS_IO CONFIG_SD_FALLBACK_CS_GPIO
#else
#define CONFIG_SD_SPI_CS_IO 34
#endif
#endif

#ifndef CONFIG_SD_FALLBACK_CS_GPIO
#define CONFIG_SD_FALLBACK_CS_GPIO CONFIG_SD_SPI_CS_IO
#endif

#ifndef CONFIG_SD_USE_FALLBACK_GPIO_CS
#define CONFIG_SD_USE_FALLBACK_GPIO_CS 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Default mount point used by the SD driver.
 */
#ifndef CONFIG_SD_MOUNT_POINT
#define CONFIG_SD_MOUNT_POINT "/sdcard"
#endif

#ifndef SD_MOUNT_POINT
#define SD_MOUNT_POINT CONFIG_SD_MOUNT_POINT
#endif

/* Backwards compatibility with legacy modules that relied on the old macro */
#ifndef MOUNT_POINT
#define MOUNT_POINT SD_MOUNT_POINT
#endif

/**
 * @brief Initialise the SPI bus, mount the SD card and print its descriptor.
 *
 * Lorsque la carte est déjà montée la fonction renvoie simplement ESP_OK.
 * La ligne CS est pilotée directement par le GPIO ESP32-S3 défini
 * dans Kconfig (par défaut : GPIO34) afin d'éviter tout trafic I²C dans les ISR.
 *
 * @return ::ESP_OK on success or a propagated error code.
 */
esp_err_t sd_mount(void);

/**
 * @brief Retrieve the descriptor of the currently mounted card.
 */
sdmmc_card_t *sd_get_card(void);

/**
 * @brief Unmount the FAT filesystem and release the SPI bus.
 *
 * @return ::ESP_OK on success or an error code when the underlying ESP-IDF
 *         helpers fail.
 */
esp_err_t sd_unmount(void);

/**
 * @brief Dump the cached card descriptor to the provided stream.
 *
 * @param stream Destination stream, typically stdout.
 * @return ::ESP_OK if the card is mounted and the information is printed.
 */
esp_err_t sd_card_print_info_stream(FILE *stream);

/**
 * @brief Convenience helper that prints the card descriptor to stdout.
 */
esp_err_t sd_card_print_info(void);

/**
 * @brief Lightweight diagnostic that toggles the CS line once.
 */
esp_err_t sd_spi_cs_selftest(void);

/**
 * @brief Query the mount state without touching the hardware.
 */
bool sd_is_mounted(void);

/**
 * @brief Always reports that CS is GPIO-driven (legacy compatibility helper).
 */
bool sd_uses_direct_cs(void);

/**
 * @brief Return the GPIO number used for the SD card CS line.
 */
int sd_get_cs_gpio(void);

/* Legacy aliases kept for compatibility with existing modules */
static inline esp_err_t sd_mmc_init(void) { return sd_mount(); }
static inline esp_err_t sd_mmc_unmount(void) { return sd_unmount(); }

#ifdef __cplusplus
}
#endif
