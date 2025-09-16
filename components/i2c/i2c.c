/*****************************************************************************
 * | File         :   i2c.c
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

#include "i2c.h"  // Include I2C driver header for I2C functions
static const char *TAG = "i2c";  // Define a tag for logging

// Global handle for the I2C master bus
// i2c_master_bus_handle_t bus_handle = NULL;
DEV_I2C_Port handle;
/**
 * @brief Initialize the I2C master interface.
 *
 * This function configures the I2C master bus. A device is not added during
 * initialization because the device address may vary per peripheral. A device
 * can later be attached with ::DEV_I2C_Set_Slave_Addr.
 *
 * @return The device handle containing the bus; the device handle will be NULL
 *         until a slave address is configured.
 */
DEV_I2C_Port DEV_I2C_Init()
{
    if (handle.bus != NULL) {
        return handle;
    }

    // Define I2C bus configuration parameters
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,       // Default clock source for I2C
        .i2c_port = EXAMPLE_I2C_MASTER_NUM,     // I2C master port number
        .scl_io_num = EXAMPLE_I2C_MASTER_SCL,   // I2C SCL (clock) pin
        .sda_io_num = EXAMPLE_I2C_MASTER_SDA,   // I2C SDA (data) pin
        .glitch_ignore_cnt = 7,                  // Ignore glitches in the I2C signal
    };

    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &handle.bus);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "I2C bus already initialized");
    } else {
        ESP_ERROR_CHECK(ret);
    }

    // No device is added here; handle.dev remains NULL until configured
    handle.dev = NULL;

    return handle;  // Return the bus handle; device handle will be assigned later
}

/**
 * @brief Probe an I2C address to check device presence.
 *
 * This helper wraps ::i2c_master_probe using the internally stored bus handle.
 *
 * @param addr 7-bit I2C address to probe.
 * @return esp_err_t ESP_OK if the device acknowledges, error code otherwise.
 */
esp_err_t DEV_I2C_Probe(uint8_t addr)
{
    if (handle.bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_probe(handle.bus, addr, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C device 0x%02X not found: %s", addr, esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Set a new I2C slave address for the device.
 * 
 * This function allows changing the I2C slave address for the specified device.
 * 
 * @param dev_handle The handle to the I2C device.
 * @param Addr The new I2C address for the device.
 */
esp_err_t DEV_I2C_Set_Slave_Addr(i2c_master_dev_handle_t *dev_handle, uint8_t Addr)
{
    // Configure the new device address
    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = EXAMPLE_I2C_MASTER_FREQUENCY,  // I2C frequency
        .device_address = Addr,                        // Set new device address
    };

    // Update the device with the new address and return status
    esp_err_t ret = i2c_master_bus_add_device(handle.bus, &i2c_dev_conf, dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C address modification failed");  // Log error if address modification fails
    }
    return ret;
}

/**
 * @brief Write a single byte to the I2C device.
 * 
 * This function sends a command byte and a value byte to the I2C device.
 * 
 * @param dev_handle The handle to the I2C device.
 * @param Cmd The command byte to send.
 * @param value The value byte to send.
 */
esp_err_t DEV_I2C_Write_Byte(i2c_master_dev_handle_t dev_handle, uint8_t Cmd, uint8_t value)
{
    uint8_t data[2] = {Cmd, value};  // Create an array with command and value
    esp_err_t ret = i2c_master_transmit(dev_handle, data, sizeof(data), 100);  // Send the data to the device
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write byte failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Read a single byte from the I2C device.
 * 
 * This function reads a byte of data from the I2C device.
 * 
 * @param dev_handle The handle to the I2C device.
 * @return The byte read from the device.
 */
esp_err_t DEV_I2C_Read_Byte(i2c_master_dev_handle_t dev_handle, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = i2c_master_receive(dev_handle, value, 1, 100);  // Read a byte from the device
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read byte failed: %s", esp_err_to_name(ret));
    }
    return ret;  // Return status
}

/**
 * @brief Read a word (2 bytes) from the I2C device.
 * 
 * This function reads two bytes (a word) from the I2C device.
 * The data is received by sending a command byte and receiving the data.
 * 
 * @param dev_handle The handle to the I2C device.
 * @param Cmd The command byte to send.
 * @return The word read from the device (combined two bytes).
 */
esp_err_t DEV_I2C_Read_Word(i2c_master_dev_handle_t dev_handle, uint8_t Cmd, uint16_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t data[2] = {Cmd};  // Create an array with the command byte
    esp_err_t ret = i2c_master_transmit_receive(dev_handle, data, 1, data, 2, 100);  // Send command and receive two bytes
    if (ret == ESP_OK) {
        *value = (data[1] << 8) | data[0];  // Combine the two bytes into a word (16-bit)
    } else {
        ESP_LOGE(TAG, "I2C read word failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Write multiple bytes to the I2C device.
 * 
 * This function sends a block of data to the I2C device.
 * 
 * @param dev_handle The handle to the I2C device.
 * @param pdata Pointer to the data to send.
 * @param len The number of bytes to send.
 */
esp_err_t DEV_I2C_Write_Nbyte(i2c_master_dev_handle_t dev_handle, uint8_t *pdata, uint8_t len)
{
    esp_err_t ret = i2c_master_transmit(dev_handle, pdata, len, 100);  // Transmit the data block
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write %d bytes failed: %s", len, esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Read multiple bytes from the I2C device.
 * 
 * This function reads multiple bytes from the I2C device.
 * The function sends a command byte and receives the specified number of bytes.
 * 
 * @param dev_handle The handle to the I2C device.
 * @param Cmd The command byte to send.
 * @param pdata Pointer to the buffer where received data will be stored.
 * @param len The number of bytes to read.
 */
esp_err_t DEV_I2C_Read_Nbyte(i2c_master_dev_handle_t dev_handle, uint8_t Cmd, uint8_t *pdata, uint8_t len)
{
    esp_err_t ret = i2c_master_transmit_receive(dev_handle, &Cmd, 1, pdata, len, 100);  // Send command and receive data
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read %d bytes failed: %s", len, esp_err_to_name(ret));
    }
    return ret;
}
