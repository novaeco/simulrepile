#include "sensors.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/uart.h"

#define I2C_TIMEOUT_MS 1000
#define UART_TIMEOUT_MS 1000

static const char *TAG = "sensors";

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

esp_err_t sensors_read(const terrarium_hw_t *hw, sensor_data_t *out_data)
{
    if (!out_data) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    /* ----------- SHT31 (Température/Humidité) ----------- */
    uint8_t sht_cmd[2] = {0x24, 0x00};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ESP_GOTO_ON_FALSE(cmd, ESP_ERR_NO_MEM, err, TAG, "cmd alloc failed");
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (hw->sht31_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, sht_cmd, sizeof(sht_cmd), true);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(hw->i2c_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    ESP_RETURN_ON_ERROR(ret, TAG, "SHT31 start failed");
    vTaskDelay(pdMS_TO_TICKS(15));
    uint8_t sht_data[6];
    cmd = i2c_cmd_link_create();
    ESP_GOTO_ON_FALSE(cmd, ESP_ERR_NO_MEM, err, TAG, "cmd alloc failed");
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (hw->sht31_addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, sht_data, sizeof(sht_data), I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(hw->i2c_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    ESP_RETURN_ON_ERROR(ret, TAG, "SHT31 read failed");
    uint16_t raw_t = ((uint16_t)sht_data[0] << 8) | sht_data[1];
    uint16_t raw_h = ((uint16_t)sht_data[3] << 8) | sht_data[4];
    out_data->temperature_c = -45.0f + 175.0f * (float)raw_t / 65535.0f;
    out_data->humidity_pct = 100.0f * (float)raw_h / 65535.0f;

    /* ----------- BH1750 (Luminosité) ----------- */
    uint8_t bh_cmd = 0x10; /* High-res mode */
    cmd = i2c_cmd_link_create();
    ESP_GOTO_ON_FALSE(cmd, ESP_ERR_NO_MEM, err, TAG, "cmd alloc failed");
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (hw->bh1750_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, bh_cmd, true);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(hw->i2c_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    ESP_RETURN_ON_ERROR(ret, TAG, "BH1750 start failed");
    vTaskDelay(pdMS_TO_TICKS(180));
    uint8_t bh_data[2];
    cmd = i2c_cmd_link_create();
    ESP_GOTO_ON_FALSE(cmd, ESP_ERR_NO_MEM, err, TAG, "cmd alloc failed");
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (hw->bh1750_addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, bh_data, sizeof(bh_data), I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(hw->i2c_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    ESP_RETURN_ON_ERROR(ret, TAG, "BH1750 read failed");
    uint16_t raw_lux = ((uint16_t)bh_data[0] << 8) | bh_data[1];
    out_data->luminosity_lux = raw_lux / 1.2f;

    /* ----------- MH-Z19B (CO2) ----------- */
    uint8_t tx[9] = {0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0};
    uint8_t checksum = 0;
    for (int i = 1; i < 8; ++i) {
        checksum += tx[i];
    }
    tx[8] = 0xFF - checksum + 1;
    ESP_RETURN_ON_ERROR(uart_write_bytes(hw->uart_port, (const char *)tx, sizeof(tx)), TAG, "uart_write_bytes failed");
    uint8_t rx[9];
    int len = uart_read_bytes(hw->uart_port, rx, sizeof(rx), pdMS_TO_TICKS(UART_TIMEOUT_MS));
    if (len == 9 && rx[0] == 0xFF && rx[1] == 0x86) {
        out_data->co2_ppm = (float)((rx[2] << 8) | rx[3]);
    } else {
        ESP_LOGE(TAG, "MH-Z19B read failed");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
err:
    if (cmd) {
        i2c_cmd_link_delete(cmd);
    }
    return ESP_ERR_NO_MEM;
}
