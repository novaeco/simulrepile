#include "sensors.h"
#include "i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>
#include <stdbool.h>

#define SHT31_ADDR 0x44
#define TMP117_ADDR 0x48
#define BH1750_ADDR_LOW 0x23
#define BH1750_ADDR_HIGH 0x5C
#define VEML6075_ADDR 0x10

#define BH1750_CMD_POWER_ON 0x01
#define BH1750_CMD_RESET 0x07
#define BH1750_CMD_ONE_TIME_HIGH_RES 0x20

#define VEML6075_DEFAULT_CONF 0x18U
#define VEML6075_REG_CONF 0x00
#define VEML6075_REG_UVA 0x07
#define VEML6075_REG_UVB 0x09
#define VEML6075_REG_UVCOMP1 0x0A
#define VEML6075_REG_UVCOMP2 0x0B
#define VEML6075_REG_ID 0x0C

#define VEML6075_UVA_A_COEF 2.22f
#define VEML6075_UVA_B_COEF 1.33f
#define VEML6075_UVB_C_COEF 2.95f
#define VEML6075_UVB_D_COEF 1.74f
#define VEML6075_UVA_RESP 0.001461f
#define VEML6075_UVB_RESP 0.002591f
#define VEML6075_UV_INDEX_MAX 11.0f

#define BH1750_CONVERSION_FACTOR 1.2f

static const TickType_t BH1750_MEAS_DELAY_TICKS = pdMS_TO_TICKS(180);

static const char *TAG = "sensors_real";
static i2c_master_dev_handle_t sht31_dev = NULL;
static i2c_master_dev_handle_t tmp117_dev = NULL;
static i2c_master_dev_handle_t bh1750_dev = NULL;
static i2c_master_dev_handle_t veml6075_dev = NULL;

static esp_err_t bh1750_init(void)
{
    const uint8_t addresses[] = {BH1750_ADDR_LOW, BH1750_ADDR_HIGH};
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        if (DEV_I2C_Probe(addresses[i]) != ESP_OK) {
            continue;
        }
        esp_err_t ret = DEV_I2C_Set_Slave_Addr(&bh1750_dev, addresses[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add BH1750 at 0x%02X: %s", addresses[i], esp_err_to_name(ret));
            continue;
        }
        uint8_t cmd = BH1750_CMD_POWER_ON;
        ret = DEV_I2C_Write_Nbyte(bh1750_dev, &cmd, 1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to power on BH1750: %s", esp_err_to_name(ret));
            i2c_master_bus_rm_device(bh1750_dev);
            bh1750_dev = NULL;
            continue;
        }
        cmd = BH1750_CMD_RESET;
        ret = DEV_I2C_Write_Nbyte(bh1750_dev, &cmd, 1);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "BH1750 reset command failed: %s", esp_err_to_name(ret));
        }
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t veml6075_init(void)
{
    if (DEV_I2C_Probe(VEML6075_ADDR) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = DEV_I2C_Set_Slave_Addr(&veml6075_dev, VEML6075_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add VEML6075: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t cfg[3] = {
        VEML6075_REG_CONF,
        (uint8_t)(VEML6075_DEFAULT_CONF & 0xFF),
        (uint8_t)((VEML6075_DEFAULT_CONF >> 8) & 0xFF),
    };
    ret = DEV_I2C_Write_Nbyte(veml6075_dev, cfg, sizeof(cfg));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure VEML6075: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(veml6075_dev);
        veml6075_dev = NULL;
        return ret;
    }

    uint8_t id_buf[2] = {0};
    ret = DEV_I2C_Read_Nbyte(veml6075_dev, VEML6075_REG_ID, id_buf, sizeof(id_buf));
    if (ret == ESP_OK) {
        uint16_t id = (uint16_t)id_buf[0] | ((uint16_t)id_buf[1] << 8);
        if ((id & 0xFF) != 0x26) {
            ESP_LOGW(TAG, "Unexpected VEML6075 ID: 0x%04X", id);
        }
    } else {
        ESP_LOGW(TAG, "Unable to read VEML6075 ID: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

static esp_err_t veml6075_read16(uint8_t reg, uint16_t *value)
{
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buf[2] = {0};
    esp_err_t ret = DEV_I2C_Read_Nbyte(veml6075_dev, reg, buf, sizeof(buf));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "VEML6075 reg 0x%02X read failed: %s", reg, esp_err_to_name(ret));
        return ret;
    }
    *value = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return ESP_OK;
}

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

    if (bh1750_init() == ESP_OK) {
        any_device = true;
    } else {
        bh1750_dev = NULL;
    }

    if (veml6075_init() == ESP_OK) {
        any_device = true;
    } else {
        veml6075_dev = NULL;
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

static float sensors_real_read_lux(void)
{
    if (bh1750_dev == NULL) {
        return NAN;
    }

    uint8_t cmd = BH1750_CMD_ONE_TIME_HIGH_RES;
    if (DEV_I2C_Write_Nbyte(bh1750_dev, &cmd, 1) != ESP_OK) {
        return NAN;
    }

    vTaskDelay(BH1750_MEAS_DELAY_TICKS);

    uint8_t buf[2] = {0};
    esp_err_t ret = i2c_master_receive(bh1750_dev, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BH1750 read failed: %s", esp_err_to_name(ret));
        return NAN;
    }

    uint16_t raw = ((uint16_t)buf[0] << 8) | buf[1];
    float lux = (float)raw / BH1750_CONVERSION_FACTOR;
    if (lux < 0.0f) {
        lux = 0.0f;
    }
    return lux;
}

static sensor_uv_data_t sensors_real_read_uv(void)
{
    sensor_uv_data_t data = {
        .uva = NAN,
        .uvb = NAN,
        .uv_index = NAN,
    };

    if (veml6075_dev == NULL) {
        return data;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    uint16_t raw_uva = 0;
    uint16_t raw_uvb = 0;
    uint16_t raw_comp1 = 0;
    uint16_t raw_comp2 = 0;

    if (veml6075_read16(VEML6075_REG_UVA, &raw_uva) != ESP_OK ||
        veml6075_read16(VEML6075_REG_UVB, &raw_uvb) != ESP_OK ||
        veml6075_read16(VEML6075_REG_UVCOMP1, &raw_comp1) != ESP_OK ||
        veml6075_read16(VEML6075_REG_UVCOMP2, &raw_comp2) != ESP_OK) {
        return data;
    }

    float uva_calc = (float)raw_uva - (VEML6075_UVA_A_COEF * (float)raw_comp1) - (VEML6075_UVA_B_COEF * (float)raw_comp2);
    float uvb_calc = (float)raw_uvb - (VEML6075_UVB_C_COEF * (float)raw_comp1) - (VEML6075_UVB_D_COEF * (float)raw_comp2);

    if (uva_calc < 0.0f) {
        uva_calc = 0.0f;
    }
    if (uvb_calc < 0.0f) {
        uvb_calc = 0.0f;
    }

    data.uva = uva_calc * VEML6075_UVA_RESP;
    data.uvb = uvb_calc * VEML6075_UVB_RESP;
    data.uv_index = (data.uva + data.uvb) * 0.5f;

    if (data.uv_index < 0.0f) {
        data.uv_index = 0.0f;
    } else if (data.uv_index > VEML6075_UV_INDEX_MAX) {
        data.uv_index = VEML6075_UV_INDEX_MAX;
    }

    return data;
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
    if (bh1750_dev) {
        i2c_master_bus_rm_device(bh1750_dev);
        bh1750_dev = NULL;
    }
    if (veml6075_dev) {
        i2c_master_bus_rm_device(veml6075_dev);
        veml6075_dev = NULL;
    }
}

const sensor_driver_t sensors_real_driver = {
    .init = sensors_real_init,
    .read_temperature = sensors_real_read_temperature,
    .read_humidity = sensors_real_read_humidity,
    .read_lux = sensors_real_read_lux,
    .read_uv = sensors_real_read_uv,
    .deinit = sensors_real_deinit,
};

