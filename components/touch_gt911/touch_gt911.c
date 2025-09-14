#include "touch_gt911.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "esp_log.h"

#define TAG "gt911"

#define I2C_PORT I2C_NUM_0
#define I2C_SCL 20
#define I2C_SDA 19
#define GT911_ADDR 0x5D
#define GPIO_RST 38
#define GPIO_INT 0

static lv_indev_t *indev;

static esp_err_t gt911_write(uint16_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[2];
    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, GT911_ADDR << 1 | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, buf, 2, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);
    esp_err_t res = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return res;
}

static esp_err_t gt911_read(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t buf[2];
    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, GT911_ADDR << 1 | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, buf, 2, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, GT911_ADDR << 1 | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t res = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return res;
}

static void gt911_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint8_t status;
    if (gt911_read(0x814E, &status, 1) != ESP_OK || !(status & 0x80)) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint8_t buf[4];
    if (gt911_read(0x8150, buf, sizeof(buf)) != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t x = (buf[1] << 8) | buf[0];
    uint16_t y = (buf[3] << 8) | buf[2];
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;

    uint8_t clear = 0;
    gt911_write(0x814E, &clear, 1);
}

void touch_gt911_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);

    gpio_set_direction(GPIO_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_INT, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(GPIO_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(GPIO_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_direction(GPIO_INT, GPIO_MODE_INPUT);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = gt911_read_cb;
    indev = lv_indev_drv_register(&indev_drv);
    ESP_LOGI(TAG, "GT911 initialized");
}
