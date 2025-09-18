#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CH422G_I2C_PORT I2C_NUM_0
#define CH422G_I2C_SDA 8
#define CH422G_I2C_SCL 9

#ifndef CONFIG_CH422G_I2C_ADDRESS
#define CONFIG_CH422G_I2C_ADDRESS 0x20u
#endif

#ifndef CONFIG_CH422G_AUTOSCAN_ADDRESSES
#define CONFIG_CH422G_AUTOSCAN_ADDRESSES 0
#endif

#ifndef CH422G_ADDR_MIN
#define CH422G_ADDR_MIN 0x20u
#endif

#ifndef CH422G_ADDR_MAX
#define CH422G_ADDR_MAX 0x27u
#endif

#define CH422G_DEFAULT_ADDR CONFIG_CH422G_I2C_ADDRESS
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
