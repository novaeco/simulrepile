#include "sensors.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/uart.h"

#define I2C_TIMEOUT_MS 1000

static const char *TAG = "sensors";

esp_err_t sensors_init(const terrarium_hw_t *hw)
{
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_NC,
        .scl_io_num = GPIO_NUM_NC,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(hw->i2c_port, &i2c_conf), TAG, "i2c_param_config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(hw->i2c_port, i2c_conf.mode, 0, 0, 0), TAG, "i2c_driver_install failed");
    /* SPI/UART init can be added here with ESP_RETURN_ON_ERROR */
    return ESP_OK;
}

esp_err_t sensors_read(const terrarium_hw_t *hw, sensor_data_t *out_data)
{
    if (!out_data) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Lecture sécurisée avec borne temporelle */
    uint8_t rx_buf[8] = {0};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ESP_GOTO_ON_FALSE(cmd, ESP_ERR_NO_MEM, err, TAG, "cmd alloc failed");
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, 0x00, true); /* Adresse capteur placeholder */
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(hw->i2c_port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    ESP_RETURN_ON_ERROR(ret, TAG, "i2c_master_cmd_begin failed");

    /* Parsing brut -> structure */
    out_data->temperature_c = 25.0f; /* valeurs factices */
    out_data->humidity_pct = 50.0f;
    out_data->luminosity_lux = 100.0f;
    out_data->co2_ppm = 400.0f;
    return ESP_OK;
err:
    i2c_cmd_link_delete(cmd);
    return ESP_ERR_NO_MEM;
}
