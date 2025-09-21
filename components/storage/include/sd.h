#pragma once

#include <stdbool.h>
#include <stdio.h>

#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**\n * @brief Default mount point used by the storage driver.\n */
#define SD_MOUNT_POINT "/sdcard"

/* Backwards compatibility with legacy modules that relied on the old macro */
#ifndef MOUNT_POINT
#define MOUNT_POINT SD_MOUNT_POINT
#endif

/**\n * @brief Initialise the SPI bus, mount the SD card and print its descriptor.\n *\n * When the card is already mounted the function simply returns the cached\n * handle. The chip-select line is driven through the CH422G expander by\n * default; an optional direct GPIO fallback can be enabled in Kconfig.\n *\n * @param[out] out_card Optional pointer that will receive the mounted card\n *                      descriptor.\n * @return ::ESP_OK on success or a propagated error code.\n */
esp_err_t sd_mount(sdmmc_card_t **out_card);

/**\n * @brief Unmount the FAT filesystem and release the SPI bus.\n *\n * The chip-select line is released to its inactive level on both the expander\n * and the optional direct GPIO path.\n *\n * @return ::ESP_OK on success or an error code when the underlying ESP-IDF\n *         helpers fail.\n */
esp_err_t sd_unmount(void);

/**\n * @brief Dump the cached card descriptor to the provided stream.\n *\n * @param stream Destination stream, typically stdout.\n * @return ::ESP_OK if the card is mounted and the information is printed.\n */
esp_err_t sd_card_print_info_stream(FILE *stream);

/**\n * @brief Convenience helper that prints the card descriptor to stdout.\n */
esp_err_t sd_card_print_info(void);

/**\n * @brief Lightweight diagnostic that toggles the CS line once.\n */
esp_err_t sd_spi_cs_selftest(void);

/**\n * @brief Query the mount state without touching the hardware.\n */
bool sd_is_mounted(void);

/**\n * @brief Report whether CS is currently driven by a direct GPIO path.\n */
bool sd_uses_direct_cs(void);

/* Legacy aliases kept for compatibility with existing modules */
static inline esp_err_t sd_mmc_init(void) { return sd_mount(NULL); }
static inline esp_err_t sd_mmc_unmount(void) { return sd_unmount(); }

#ifdef __cplusplus
}
#endif
