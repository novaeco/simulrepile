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
#include "esp_check.h"
#include "esp_rom_sys.h"
static const char *TAG = "i2c";  // Define a tag for logging

// Global handle for the I2C master bus
// i2c_master_bus_handle_t bus_handle = NULL;
DEV_I2C_Port handle;

#define I2C_MAX_REGISTERED_DEVICES 8
static i2c_master_dev_handle_t *s_registered_handle_ptrs[I2C_MAX_REGISTERED_DEVICES];

static void i2c_register_handle_slot(i2c_master_dev_handle_t *slot)
{
    if (slot == NULL) {
        return;
    }
    for (size_t i = 0; i < I2C_MAX_REGISTERED_DEVICES; ++i) {
        if (s_registered_handle_ptrs[i] == slot) {
            return;
        }
    }
    for (size_t i = 0; i < I2C_MAX_REGISTERED_DEVICES; ++i) {
        if (s_registered_handle_ptrs[i] == NULL) {
            s_registered_handle_ptrs[i] = slot;
            return;
        }
    }
    ESP_LOGW(TAG, "I2C handle registry full; recovery may leave stale pointers");
}

static void i2c_release_handles(void)
{
    if (handle.dev != NULL) {
        esp_err_t ret = i2c_master_bus_rm_device(handle.dev);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove I2C device handle: %s", esp_err_to_name(ret));
        }
        handle.dev = NULL;
    }
    if (handle.bus != NULL) {
        esp_err_t ret = i2c_del_master_bus(handle.bus);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete I2C master bus: %s", esp_err_to_name(ret));
        }
        handle.bus = NULL;
    }
    for (size_t i = 0; i < I2C_MAX_REGISTERED_DEVICES; ++i) {
        if (s_registered_handle_ptrs[i] != NULL) {
            *s_registered_handle_ptrs[i] = NULL;
        }
    }
}

static esp_err_t i2c_bus_drive_lines_idle(void)
{
    const uint64_t pin_mask = (1ULL << CONFIG_I2C_MASTER_SDA_GPIO) |
                              (1ULL << CONFIG_I2C_MASTER_SCL_GPIO);

    gpio_config_t od_cfg = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&od_cfg), TAG, "gpio_config recovery");

    gpio_set_level(EXAMPLE_I2C_MASTER_SDA, 1);
    gpio_set_level(EXAMPLE_I2C_MASTER_SCL, 1);
    esp_rom_delay_us(5);

    for (int i = 0; i < 9; ++i) {
        gpio_set_level(EXAMPLE_I2C_MASTER_SCL, 0);
        esp_rom_delay_us(5);
        gpio_set_level(EXAMPLE_I2C_MASTER_SCL, 1);
        esp_rom_delay_us(5);
    }

    // STOP condition to release any slave still holding SDA
    gpio_set_level(EXAMPLE_I2C_MASTER_SDA, 0);
    esp_rom_delay_us(5);
    gpio_set_level(EXAMPLE_I2C_MASTER_SCL, 1);
    esp_rom_delay_us(5);
    gpio_set_level(EXAMPLE_I2C_MASTER_SDA, 1);
    esp_rom_delay_us(5);

    gpio_config_t input_cfg = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input_cfg), TAG, "gpio_config release");

#if CONFIG_I2C_MASTER_ENABLE_INTERNAL_PULLUPS
    gpio_set_pull_mode(EXAMPLE_I2C_MASTER_SDA, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(EXAMPLE_I2C_MASTER_SCL, GPIO_PULLUP_ONLY);
#endif

    return ESP_OK;
}

esp_err_t DEV_I2C_Bus_Recover(void)
{
    ESP_LOGW(TAG,
             "Attempting I2C bus recovery on SDA=%d SCL=%d",
             CONFIG_I2C_MASTER_SDA_GPIO, CONFIG_I2C_MASTER_SCL_GPIO);

    i2c_release_handles();

    esp_err_t ret = i2c_bus_drive_lines_idle();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus recovery failed: %s", esp_err_to_name(ret));
    }
    return ret;
}
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

#if CONFIG_I2C_MASTER_ENABLE_INTERNAL_PULLUPS
    // Ensure the internal pull-ups are enabled in addition to the external ones
    gpio_set_pull_mode(EXAMPLE_I2C_MASTER_SDA, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(EXAMPLE_I2C_MASTER_SCL, GPIO_PULLUP_ONLY);
#endif

    // Define I2C bus configuration parameters
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,      // Default clock source for I2C
        .i2c_port = EXAMPLE_I2C_MASTER_NUM,     // I2C master port number
        .scl_io_num = EXAMPLE_I2C_MASTER_SCL,   // I2C SCL (clock) pin
        .sda_io_num = EXAMPLE_I2C_MASTER_SDA,   // I2C SDA (data) pin
        .glitch_ignore_cnt = 7,                 // Ignore glitches in the I2C signal
    };

#if CONFIG_I2C_MASTER_ENABLE_INTERNAL_PULLUPS
    /*
     * The esp_driver_i2c master reconfigures the GPIOs during
     * i2c_new_master_bus(), clearing the pull mode we set above. Make sure the
     * controller keeps the internal pull-ups enabled so that boards without
     * external resistors (comme la Waveshare ESP32-S3 1024x600) bénéficient
     * d'une polarisation fiable du bus.
     */
    i2c_bus_config.flags.enable_internal_pullup = 1;
#endif

    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &handle.bus);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "I2C bus already initialized");
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to initialise I2C bus on SDA=%d SCL=%d: %s. Attempting recovery.",
                 CONFIG_I2C_MASTER_SDA_GPIO, CONFIG_I2C_MASTER_SCL_GPIO,
                 esp_err_to_name(ret));
        if (DEV_I2C_Bus_Recover() == ESP_OK) {
            ret = i2c_new_master_bus(&i2c_bus_config, &handle.bus);
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialise I2C bus on SDA=%d SCL=%d: %s",
                     CONFIG_I2C_MASTER_SDA_GPIO, CONFIG_I2C_MASTER_SCL_GPIO,
                     esp_err_to_name(ret));
            handle.bus = NULL;
            return handle;
        }
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

    const int max_attempts = 2;
    esp_err_t ret = ESP_FAIL;

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        ret = i2c_master_probe(handle.bus, addr, 100);
        if (ret == ESP_OK) {
            if (attempt > 1) {
                ESP_LOGI(TAG, "I2C bus recovered, device 0x%02X acknowledged", addr);
            }
            return ESP_OK;
        }

        bool recoverable = (ret == ESP_ERR_TIMEOUT) || (ret == ESP_ERR_INVALID_STATE);
        if (!recoverable || attempt == max_attempts) {
            break;
        }

        ESP_LOGW(TAG,
                 "I2C probe 0x%02X attempt %d/%d failed (%s). Recovering bus.",
                 addr, attempt, max_attempts, esp_err_to_name(ret));
        esp_err_t recover_ret = DEV_I2C_Bus_Recover();
        if (recover_ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C bus recovery failed: %s", esp_err_to_name(recover_ret));
            return recover_ret;
        }
        DEV_I2C_Port port = DEV_I2C_Init();
        if (port.bus == NULL) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    ESP_LOGE(TAG,
             "I2C device 0x%02X not found: %s. Verify VCC=3V3, pull-ups and wiring on SDA=%d / SCL=%d.",
             addr, esp_err_to_name(ret), CONFIG_I2C_MASTER_SDA_GPIO,
             CONFIG_I2C_MASTER_SCL_GPIO);
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
    if (dev_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_register_handle_slot(dev_handle);

    if (handle.bus == NULL) {
        DEV_I2C_Port port = DEV_I2C_Init();
        if (port.bus == NULL) {
            ESP_LOGE(TAG, "I2C bus not initialized");
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (Addr > 0x7F) {
        ESP_LOGE(TAG, "Invalid 7-bit I2C address 0x%02X", Addr);
        return ESP_ERR_INVALID_ARG;

    }

    if (*dev_handle != NULL) {
        esp_err_t rm_ret = i2c_master_bus_rm_device(handle.bus, *dev_handle);
        if (rm_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove existing I2C device: %s", esp_err_to_name(rm_ret));
            return rm_ret;
        }
        if (handle.dev == *dev_handle) {
            handle.dev = NULL;
        }
        *dev_handle = NULL;
    }

    // Configure the new device address
    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = EXAMPLE_I2C_MASTER_FREQUENCY,  // I2C frequency
        .device_address = Addr,                        // Set new device address
    };

    // Update the device with the new address and return status
    esp_err_t ret = i2c_master_bus_add_device(handle.bus, &i2c_dev_conf, dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device 0x%02X: %s", Addr, esp_err_to_name(ret));
        *dev_handle = NULL;
        return ret;
    }

    if (handle.dev == NULL) {
        handle.dev = *dev_handle;
    }

    return ESP_OK;
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

esp_err_t DEV_I2C_Scan(uint8_t start_addr, uint8_t end_addr, uint8_t *buffer, size_t buffer_len, size_t *found_count)
{
    if (start_addr > end_addr) {
        uint8_t tmp = start_addr;
        start_addr = end_addr;
        end_addr = tmp;
    }
    if (buffer == NULL && buffer_len > 0) {
        return ESP_ERR_INVALID_ARG;
    }

    DEV_I2C_Port port = DEV_I2C_Init();
    if (port.bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t count = 0;
    esp_err_t last_err = ESP_ERR_NOT_FOUND;
    for (uint16_t addr = start_addr; addr <= end_addr; ++addr) {
        if (addr < 0x08 || addr > 0x77) {
            continue;
        }

        esp_err_t ret = i2c_master_probe(port.bus, addr, 50);
        if (ret == ESP_OK) {
            if (buffer != NULL && count < buffer_len) {
                buffer[count] = (uint8_t)addr;
            }
            count++;
        } else if (ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG,
                     "I2C scan: bus error while probing 0x%02X (%s). Attempting recovery.",
                     (unsigned)addr, esp_err_to_name(ret));
            esp_err_t rec_ret = DEV_I2C_Bus_Recover();
            if (rec_ret != ESP_OK) {
                return rec_ret;
            }
            port = DEV_I2C_Init();
            if (port.bus == NULL) {
                return ESP_ERR_INVALID_STATE;
            }
            ret = i2c_master_probe(port.bus, addr, 50);
            if (ret == ESP_OK) {
                if (buffer != NULL && count < buffer_len) {
                    buffer[count] = (uint8_t)addr;
                }
                count++;
            } else {
                last_err = ret;
            }
        } else {
            last_err = ret;
        }
    }

    if (found_count != NULL) {
        *found_count = count;
    }

    if (count == 0) {
        if (last_err == ESP_ERR_NOT_FOUND || last_err == ESP_OK) {
            return ESP_ERR_NOT_FOUND;
        }
        return last_err;
    }

    return ESP_OK;
}
