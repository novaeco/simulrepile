#include "sensors.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include <math.h>

#define I2C_TIMEOUT_MS 1000
#define UART_TIMEOUT_MS 1000
#define UART_HANDSHAKE_TIMEOUT_MS 200

static const char *TAG = "sensors";

extern terrarium_hw_t g_terrariums[];
extern const size_t g_terrarium_count;
extern terrarium_device_status_t g_device_status[];

static uint8_t sht31_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static int find_hw_index(const terrarium_hw_t *hw)
{
    for (size_t i = 0; i < g_terrarium_count; ++i) {
        if (&g_terrariums[i] == hw) {
            return (int)i;
        }
    }
    return -1;
}

static esp_err_t read_sht31(const terrarium_hw_t *hw, float *temperature, float *humidity)
{
    esp_err_t ret = ESP_OK;
    uint8_t cmd_data[2] = {0x24, 0x00};
    uint8_t rx_data[6];

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        return ESP_ERR_NO_MEM;
    }

    ESP_GOTO_ON_ERROR(i2c_master_start(cmd), cleanup_write, TAG, "SHT31 start failed");
    ESP_GOTO_ON_ERROR(i2c_master_write_byte(cmd, (hw->sht31_addr << 1) | I2C_MASTER_WRITE, true), cleanup_write, TAG, "SHT31 addr failed");
    ESP_GOTO_ON_ERROR(i2c_master_write(cmd, cmd_data, sizeof(cmd_data), true), cleanup_write, TAG, "SHT31 command failed");
    ESP_GOTO_ON_ERROR(i2c_master_stop(cmd), cleanup_write, TAG, "SHT31 stop failed");
    ret = i2c_master_cmd_begin(hw->i2c_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));

cleanup_write:
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(15));

    cmd = i2c_cmd_link_create();
    if (!cmd) {
        return ESP_ERR_NO_MEM;
    }

    ESP_GOTO_ON_ERROR(i2c_master_start(cmd), cleanup_read, TAG, "SHT31 read start failed");
    ESP_GOTO_ON_ERROR(i2c_master_write_byte(cmd, (hw->sht31_addr << 1) | I2C_MASTER_READ, true), cleanup_read, TAG, "SHT31 read addr failed");
    ESP_GOTO_ON_ERROR(i2c_master_read(cmd, rx_data, sizeof(rx_data), I2C_MASTER_LAST_NACK), cleanup_read, TAG, "SHT31 read failed");
    ESP_GOTO_ON_ERROR(i2c_master_stop(cmd), cleanup_read, TAG, "SHT31 read stop failed");
    ret = i2c_master_cmd_begin(hw->i2c_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));

cleanup_read:
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t temp_crc = sht31_crc8(rx_data, 2);
    uint8_t humidity_crc = sht31_crc8(&rx_data[3], 2);
    if (temp_crc != rx_data[2] || humidity_crc != rx_data[5]) {
        ESP_LOGE(TAG, "SHT31 CRC mismatch: temp 0x%02X!=0x%02X, humidity 0x%02X!=0x%02X",
                 temp_crc, rx_data[2], humidity_crc, rx_data[5]);
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_t = ((uint16_t)rx_data[0] << 8) | rx_data[1];
    uint16_t raw_h = ((uint16_t)rx_data[3] << 8) | rx_data[4];
    if (temperature) {
        *temperature = -45.0f + 175.0f * (float)raw_t / 65535.0f;
    }
    if (humidity) {
        *humidity = 100.0f * (float)raw_h / 65535.0f;
    }
    return ESP_OK;
}

static esp_err_t read_bh1750(const terrarium_hw_t *hw, float *lux)
{
    esp_err_t ret = ESP_OK;
    uint8_t cmd_data = 0x10; /* High-res mode */
    uint8_t rx_data[2];

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        return ESP_ERR_NO_MEM;
    }

    ESP_GOTO_ON_ERROR(i2c_master_start(cmd), cleanup_write, TAG, "BH1750 start failed");
    ESP_GOTO_ON_ERROR(i2c_master_write_byte(cmd, (hw->bh1750_addr << 1) | I2C_MASTER_WRITE, true), cleanup_write, TAG, "BH1750 addr failed");
    ESP_GOTO_ON_ERROR(i2c_master_write_byte(cmd, cmd_data, true), cleanup_write, TAG, "BH1750 command failed");
    ESP_GOTO_ON_ERROR(i2c_master_stop(cmd), cleanup_write, TAG, "BH1750 stop failed");
    ret = i2c_master_cmd_begin(hw->i2c_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));

cleanup_write:
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(180));

    cmd = i2c_cmd_link_create();
    if (!cmd) {
        return ESP_ERR_NO_MEM;
    }

    ESP_GOTO_ON_ERROR(i2c_master_start(cmd), cleanup_read, TAG, "BH1750 read start failed");
    ESP_GOTO_ON_ERROR(i2c_master_write_byte(cmd, (hw->bh1750_addr << 1) | I2C_MASTER_READ, true), cleanup_read, TAG, "BH1750 read addr failed");
    ESP_GOTO_ON_ERROR(i2c_master_read(cmd, rx_data, sizeof(rx_data), I2C_MASTER_LAST_NACK), cleanup_read, TAG, "BH1750 read failed");
    ESP_GOTO_ON_ERROR(i2c_master_stop(cmd), cleanup_read, TAG, "BH1750 read stop failed");
    ret = i2c_master_cmd_begin(hw->i2c_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));

cleanup_read:
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        return ret;
    }

    uint16_t raw_lux = ((uint16_t)rx_data[0] << 8) | rx_data[1];
    if (lux) {
        *lux = raw_lux / 1.2f;
    }
    return ESP_OK;
}

static esp_err_t read_mhz19b(const terrarium_hw_t *hw, float *co2, uint32_t timeout_ms)
{
    uint8_t tx[9] = {0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0};
    uint8_t checksum = 0;
    for (int i = 1; i < 8; ++i) {
        checksum += tx[i];
    }
    tx[8] = 0xFF - checksum + 1;

    int written = uart_write_bytes(hw->uart_port, (const char *)tx, sizeof(tx));
    if (written < 0 || written != (int)sizeof(tx)) {
        ESP_LOGE(TAG, "uart_write_bytes failed");
        return ESP_FAIL;
    }

    uint8_t rx[9] = {0};
    int len = uart_read_bytes(hw->uart_port, rx, sizeof(rx), pdMS_TO_TICKS(timeout_ms));
    if (len == 9 && rx[0] == 0xFF && rx[1] == 0x86) {
        if (co2) {
            *co2 = (float)((rx[2] << 8) | rx[3]);
        }
        return ESP_OK;
    }

    ESP_LOGE(TAG, "MH-Z19B read failed");
    return ESP_ERR_TIMEOUT;
}

static bool handshake_sht31(const terrarium_hw_t *hw)
{
    float t = NAN;
    float h = NAN;
    esp_err_t ret = read_sht31(hw, &t, &h);
    return ret == ESP_OK && !isnan(t) && !isnan(h);
}

static bool handshake_bh1750(const terrarium_hw_t *hw)
{
    float lux = NAN;
    esp_err_t ret = read_bh1750(hw, &lux);
    return ret == ESP_OK && !isnan(lux);
}

static bool handshake_mhz19b(const terrarium_hw_t *hw)
{
    float co2 = NAN;
    esp_err_t ret = read_mhz19b(hw, &co2, UART_HANDSHAKE_TIMEOUT_MS);
    return ret == ESP_OK && !isnan(co2);
}

esp_err_t sensors_init(const terrarium_hw_t *hw)
{
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = hw->i2c_sda_gpio,
        .scl_io_num = hw->i2c_scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(hw->i2c_port, &i2c_conf), TAG, "i2c_param_config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(hw->i2c_port, i2c_conf.mode, 0, 0, 0), TAG, "i2c_driver_install failed");

    uart_config_t uart_conf = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    ESP_RETURN_ON_ERROR(uart_param_config(hw->uart_port, &uart_conf), TAG, "uart_param_config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(hw->uart_port, hw->uart_tx_gpio, hw->uart_rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart_set_pin failed");
    ESP_RETURN_ON_ERROR(uart_driver_install(hw->uart_port, 256, 0, 0, NULL, 0), TAG, "uart_driver_install failed");
    return ESP_OK;
}

sensor_connection_t sensors_detect(const terrarium_hw_t *hw)
{
    sensor_connection_t status = {
        .temperature_humidity = false,
        .luminosity = false,
        .co2 = false,
    };

    if (!hw) {
        return status;
    }

    int idx = find_hw_index(hw);

    status.temperature_humidity = handshake_sht31(hw);
    status.luminosity = handshake_bh1750(hw);
    status.co2 = handshake_mhz19b(hw);

    if (idx >= 0) {
        ESP_LOGI(TAG, "Terrarium %d SHT31 %s, BH1750 %s, MH-Z19B %s",
                 idx,
                 status.temperature_humidity ? "OK" : "absent",
                 status.luminosity ? "OK" : "absent",
                 status.co2 ? "OK" : "absent");
    }

    return status;
}

esp_err_t sensors_read(const terrarium_hw_t *hw, sensor_data_t *out_data)
{
    if (!out_data) {
        return ESP_ERR_INVALID_ARG;
    }

    out_data->temperature_c = NAN;
    out_data->humidity_pct = NAN;
    out_data->luminosity_lux = NAN;
    out_data->co2_ppm = NAN;
    out_data->power_w = 0.0f;

    int idx = find_hw_index(hw);
    const sensor_connection_t *conn = NULL;
    if (idx >= 0) {
        conn = &g_device_status[idx].sensors;
    }

    bool any_attempt = false;
    esp_err_t ret = ESP_OK;

    if (conn && conn->temperature_humidity) {
        any_attempt = true;
        ret = read_sht31(hw, &out_data->temperature_c, &out_data->humidity_pct);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (conn && conn->luminosity) {
        any_attempt = true;
        ret = read_bh1750(hw, &out_data->luminosity_lux);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (conn && conn->co2) {
        any_attempt = true;
        ret = read_mhz19b(hw, &out_data->co2_ppm, UART_TIMEOUT_MS);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (!any_attempt) {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}
