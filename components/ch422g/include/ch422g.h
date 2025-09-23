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

#ifndef CONFIG_CH422G_I2C_ADDR
#define CONFIG_CH422G_I2C_ADDR 0x24
#endif

#define CH422G_I2C_ADDR_DEFAULT ((uint8_t)CONFIG_CH422G_I2C_ADDR)
#define CH422G_REG_EXIO 0x01u
#define CH422G_REG_CONFIG 0x02u

typedef enum {
    CH422G_PIN_MODE_INPUT = 0,
    CH422G_PIN_MODE_OUTPUT = 1,
} ch422g_pin_mode_t;

/**
 * @brief Initialise the CH422G expander and drive all EXIO outputs high.
 */
esp_err_t ch422g_init(void);

/**
 * @brief Configure the direction of an EXIO pin.
 *
 * @param exio_index Index in the range [1,8] matching the EXIO silkscreen.
 * @param mode       Requested direction. Both push-pull output and input modes
 *                   are supported.
 */
esp_err_t ch422g_pin_mode(uint8_t exio_index, ch422g_pin_mode_t mode);

/**
 * @brief Scan a range of 7-bit addresses looking for a CH422G acknowledgement.
 *
 * @param start_addr Inclusive start of the range (0x08–0x77).
 * @param end_addr   Inclusive end of the range.
 * @param out_addr   Optional pointer that will receive the first responding address.
 *
 * @return ESP_OK when at least one address acknowledged, ESP_ERR_NOT_FOUND otherwise.
 */
esp_err_t ch422g_scan(uint8_t start_addr, uint8_t end_addr, uint8_t *out_addr);

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

/**
 * @brief Convenience wrapper matching Arduino-style digitalWrite().
 */
static inline esp_err_t ch422g_digital_write(uint8_t exio_index, bool level)
{
    return ch422g_exio_set(exio_index, level);
}

/**
 * @brief Return the runtime-detected 7-bit I2C address of the expander.
 */
uint8_t ch422g_get_address(void);

#ifdef __cplusplus
}
#endif
