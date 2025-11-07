#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char version[32];
    char channel[32];
    char build_id[32];
    char file_name[64];
    size_t size_bytes;
    uint32_t crc32;
} updates_manifest_info_t;

typedef enum {
    UPDATES_FLASH_OUTCOME_NONE = 0,
    UPDATES_FLASH_OUTCOME_SUCCESS,
    UPDATES_FLASH_OUTCOME_ERROR,
    UPDATES_FLASH_OUTCOME_ROLLBACK,
} updates_flash_outcome_t;

typedef struct {
    updates_flash_outcome_t outcome;
    esp_err_t error;
    updates_manifest_info_t manifest;
    char partition_label[16];
} updates_flash_report_t;

/**
 * @brief Inspect the SD card for an available firmware update package.
 *
 * This function parses `/sdcard/updates/manifest.json`, verifies that
 * `update.bin` exists, computes its CRC32 and validates it against the
 * manifest metadata. If everything is consistent the manifest details are
 * copied to `out_info`.
 *
 * @param[out] out_info Optional pointer filled with manifest data when a
 *                     valid update is present.
 * @return ESP_OK when a valid update is available, ESP_ERR_NOT_FOUND when no
 *         manifest is present, or an error code if the manifest or binary is
 *         invalid.
 */
esp_err_t updates_check_available(updates_manifest_info_t *out_info);

/**
 * @brief Apply the staged SD-card update to the next OTA partition.
 *
 * The update binary is copied to the inactive OTA partition. A `.bak`
 * snapshot is kept on the SD card while flashing so the original package can
 * be restored if the process fails.
 *
 * @param expected_info Optional pointer to validate that the manifest did not
 *                      change between the availability check and the apply
 *                      request. Pass NULL to skip this verification.
 * @return ESP_OK on success. The device should be rebooted afterwards to boot
 *         into the new firmware. Other esp_err_t codes indicate the reason for
 *         failure.
 */
esp_err_t updates_apply(const updates_manifest_info_t *expected_info);

/**
 * @brief Return the outcome of the most recent flashing attempt.
 *
 * The result is persisted on the SD card (`/sdcard/updates/last_flash.json`).
 *
 * @param[out] out_report Pointer receiving the stored report.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no history exists.
 */
esp_err_t updates_get_last_flash_report(updates_flash_report_t *out_report);

/**
 * @brief Translate a flash outcome enum to a constant ASCII string.
 */
const char *updates_flash_outcome_to_string(updates_flash_outcome_t outcome);

/**
 * @brief Finalize OTA bookkeeping at boot (rollback detection, validation).
 *
 * This should be invoked once during boot before checking for new packages.
 */
esp_err_t updates_finalize_boot_state(void);

#ifdef __cplusplus
}
#endif
