#include "sensors.h"
#include "i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>
#include <stdbool.h>

#define SHT31_ADDR 0x44
#define TMP117_ADDR 0x48

static const char *TAG = "sensors_real";
static i2c_master_dev_handle_t sht31_dev = NULL;
static i2c_master_dev_handle_t tmp117_dev = NULL;

static esp_err_t sensors_real_init(void)
{
    DEV_I2C_Port port = DEV_I2C_Init();
    (void)port; // bus handle kept internally

    bool any_device = false;
    if (DEV_I2C_Probe(SHT31_ADDR) == ESP_OK) {
        esp_err_t ret = DEV_I2C_Set_Slave_Addr(&sht31_dev, SHT31_ADDR);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set SHT31 address: %s", esp_err_to_name(ret));
            return ESP_FAIL;
        }
        any_device = true;
    } else {
        sht31_dev = NULL;
    }

    if (DEV_I2C_Probe(TMP117_ADDR) == ESP_OK) {
        esp_err_t ret = DEV_I2C_Set_Slave_Addr(&tmp117_dev, TMP117_ADDR);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set TMP117 address: %s", esp_err_to_name(ret));
            return ESP_FAIL;
        }
        any_device = true;
    } else {
        tmp117_dev = NULL;
    }

    if (!any_device) {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

static float sensors_real_read_temperature(void)
{
    float sum = 0.0f;
    int count = 0;

    if (tmp117_dev) {
        uint16_t raw_tmp = 0;
        if (DEV_I2C_Read_Word(tmp117_dev, 0x00, &raw_tmp) == ESP_OK) {
            sum += (int16_t)raw_tmp * 0.0078125f;
            count++;
        }
    }

    if (sht31_dev) {
        uint8_t cmd[2] = {0x2C, 0x06};
        if (DEV_I2C_Write_Nbyte(sht31_dev, cmd, 2) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(15));
            uint8_t data[6] = {0};
            if (DEV_I2C_Read_Nbyte(sht31_dev, 0x00, data, 6) == ESP_OK) {
                uint16_t raw_sht = (data[0] << 8) | data[1];
                sum += -45.0f + 175.0f * ((float)raw_sht / 65535.0f);
                count++;
            }
        }
    }

    if (count == 0) {
        ESP_LOGW(TAG, "No temperature sensor available");
        return NAN;
    }

    return sum / (float)count;
}

static float sensors_real_read_humidity(void)
{
    if (sht31_dev == NULL) {
        return NAN;
    }

    uint8_t cmd[2] = {0x2C, 0x06};
    if (DEV_I2C_Write_Nbyte(sht31_dev, cmd, 2) != ESP_OK) {
        return NAN;
    }
    vTaskDelay(pdMS_TO_TICKS(15));
    uint8_t data[6] = {0};
    if (DEV_I2C_Read_Nbyte(sht31_dev, 0x00, data, 6) != ESP_OK) {
        return NAN;
    }
    uint16_t raw_hum = (data[3] << 8) | data[4];
    return 100.0f * ((float)raw_hum / 65535.0f);
}

static void sensors_real_deinit(void)
{
    if (sht31_dev) {
        i2c_master_bus_rm_device(sht31_dev);
        sht31_dev = NULL;
    }
    if (tmp117_dev) {
        i2c_master_bus_rm_device(tmp117_dev);
        tmp117_dev = NULL;
    }
}

const sensor_driver_t sensors_real_driver = {
    .init = sensors_real_init,
    .read_temperature = sensors_real_read_temperature,
    .read_humidity = sensors_real_read_humidity,
    .deinit = sensors_real_deinit,
};

