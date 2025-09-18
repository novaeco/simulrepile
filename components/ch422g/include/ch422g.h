#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CH422G_I2C_PORT I2C_NUM_0
#define CH422G_I2C_SDA 8
#define CH422G_I2C_SCL 9

/* TODO: confirm the actual 7-bit address and EXIO register offset on the
 * Waveshare carrier revision in use. */
#define CH422G_DEFAULT_ADDR 0x20u
#define CH422G_REG_EXIO 0x01u

/**
 * @brief Initialise the CH422G expander and drive all EXIO outputs high.
 */
esp_err_t ch422g_init(void);

/**
 * @brief Update a single EXIO output level.
 *
 * @param exio_index Index in the range [1,8] matching the EXIO number printed on the silkscreen.
 * @param level Logical level (true = high / released, false = low / asserted).
 */
esp_err_t ch422g_exio_set(uint8_t exio_index, bool level);

/**
 * @brief Read the last written shadow register.
 */
uint8_t ch422g_exio_shadow_get(void);

#ifdef __cplusplus
}
#endif
