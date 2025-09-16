/*****************************************************************************
 * | File         :   i2c.h
 * | Author       :   Waveshare team
 * | Function     :   Hardware underlying interface
 * | Info         :
 * |                 I2C driver code for I2C communication.
 * ----------------
 * | This version :   V1.0
 * | Date         :   2024-11-26
 * | Info         :   Basic version
 *
 ******************************************************************************/

#ifndef __I2C_H
#define __I2C_H

#include <stdio.h>          // Standard input/output library
#include <string.h>         // String manipulation functions
#include "driver/i2c_master.h"    // ESP32 I2C master driver library
#include "esp_log.h"        // ESP32 logging library for debugging
#include "gpio.h"           // GPIO header for pin configuration

// Define the SDA (data) and SCL (clock) pins for I2C communication
#define EXAMPLE_I2C_MASTER_SDA GPIO_NUM_8  // SDA pin
#define EXAMPLE_I2C_MASTER_SCL GPIO_NUM_9  // SCL pin

// Define the I2C frequency (400 kHz)
#define EXAMPLE_I2C_MASTER_FREQUENCY (400 * 1000)  // I2C speed

// Define the I2C master port number (I2C_NUM_0 in this case)
#define EXAMPLE_I2C_MASTER_NUM I2C_NUM_0


typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
} DEV_I2C_Port;
// Function prototypes for I2C communication

/**
 * @brief Initialize the I2C master interface.
 *
 * This function sets up the I2C master interface, including the SDA/SCL pins
 * and clock frequency, and prepares the device handle.
 *
 * @return A handle to the I2C device if initialization is successful, NULL if failure.
 */
DEV_I2C_Port DEV_I2C_Init();

/**
 * @brief Probe an I2C address to verify device presence.
 *
 * @param addr 7-bit I2C address to probe.
 * @return esp_err_t ESP_OK if acknowledged, error code otherwise.
 */
esp_err_t DEV_I2C_Probe(uint8_t addr);

/**
 * @brief Set a new I2C slave address for the device.
 * 
 * This function allows you to change the slave address of an I2C device during runtime.
 * 
 * @param dev_handle The handle to the I2C device.
 * @param Addr The new I2C address to set for the device.
 */
esp_err_t DEV_I2C_Set_Slave_Addr(i2c_master_dev_handle_t *dev_handle, uint8_t Addr);

/**
 * @brief Write a single byte to the I2C device.
 * 
 * This function sends a command byte and a data byte to the I2C device.
 * 
 * @param dev_handle The handle to the I2C device.
 * @param Cmd The command byte to send to the device.
 * @param value The value byte to send to the device.
 */
esp_err_t DEV_I2C_Write_Byte(i2c_master_dev_handle_t dev_handle, uint8_t Cmd, uint8_t value);

/**
 * @brief Read a single byte from the I2C device.
 *
 * This function reads one byte of data from the I2C device.
 *
 * @param dev_handle The handle to the I2C device.
 * @param value Pointer to variable where the read byte will be stored.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t DEV_I2C_Read_Byte(i2c_master_dev_handle_t dev_handle, uint8_t *value);

/**
 * @brief Read a word (2 bytes) from the I2C device.
 *
 * This function sends a command byte and reads two bytes from the I2C device.
 * The two bytes are combined into a 16-bit word.
 *
 * @param dev_handle The handle to the I2C device.
 * @param Cmd The command byte to send.
 * @param value Pointer to variable where the read word will be stored.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t DEV_I2C_Read_Word(i2c_master_dev_handle_t dev_handle, uint8_t Cmd, uint16_t *value);

/**
 * @brief Write multiple bytes to the I2C device.
 *
 * This function sends a block of data (multiple bytes) to the I2C device.
 *
 * @param dev_handle The handle to the I2C device.
 * @param pdata A pointer to the data to write.
 * @param len The number of bytes to write.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t DEV_I2C_Write_Nbyte(i2c_master_dev_handle_t dev_handle, uint8_t *pdata, uint8_t len);

/**
 * @brief Read multiple bytes from the I2C device.
 *
 * This function sends a command byte and reads multiple bytes from the I2C device.
 *
 * @param dev_handle The handle to the I2C device.
 * @param Cmd The command byte to send.
 * @param pdata A pointer to the buffer to store the received data.
 * @param len The number of bytes to read.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t DEV_I2C_Read_Nbyte(i2c_master_dev_handle_t dev_handle, uint8_t Cmd, uint8_t *pdata, uint8_t len);

#endif
