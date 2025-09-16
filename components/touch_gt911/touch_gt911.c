#include "touch_gt911.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "esp_log.h"
#include "input_gestures.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

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
    if (cmd == NULL) {
        ESP_LOGE(TAG, "i2c_cmd_link_create failed");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = i2c_master_start(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_start failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = i2c_master_write_byte(cmd, GT911_ADDR << 1 | I2C_MASTER_WRITE, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_write_byte failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = i2c_master_write(cmd, buf, sizeof(buf), true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_write (register) failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = i2c_master_write(cmd, data, len, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_write (payload) failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = i2c_master_stop(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_stop failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin failed: %s", esp_err_to_name(err));
    }

cleanup:
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t gt911_read(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t buf[2];
    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        ESP_LOGE(TAG, "i2c_cmd_link_create failed");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = i2c_master_start(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_start failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = i2c_master_write_byte(cmd, GT911_ADDR << 1 | I2C_MASTER_WRITE, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_write_byte failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = i2c_master_write(cmd, buf, sizeof(buf), true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_write (register) failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = i2c_master_start(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_start (read) failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = i2c_master_write_byte(cmd, GT911_ADDR << 1 | I2C_MASTER_READ, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_write_byte (read) failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_read failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = i2c_master_stop(cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_stop failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin failed: %s", esp_err_to_name(err));
    }

cleanup:
    i2c_cmd_link_delete(cmd);
    return err;
}

static void gt911_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint8_t status;
    if (gt911_read(0x814E, &status, 1) != ESP_OK || !(status & 0x80)) {
        data->state = LV_INDEV_STATE_RELEASED;
        input_gestures_update(NULL, 0);
        return;
    }

    uint8_t touches = status & 0x0F;
    if (touches == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        input_gestures_update(NULL, 0);
        return;
    }

    uint8_t buf[16];
    size_t read_len = touches * 8;
    if (read_len > sizeof(buf)) {
        read_len = sizeof(buf);
    }
    if (gt911_read(0x8150, buf, read_len) != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        input_gestures_update(NULL, 0);
        return;
    }

    lv_point_t pts[2];
    for (uint8_t i = 0; i < touches && i < 2; i++) {
        uint16_t x = (buf[i * 8 + 1] << 8) | buf[i * 8 + 0];
        uint16_t y = (buf[i * 8 + 3] << 8) | buf[i * 8 + 2];
        pts[i].x = x;
        pts[i].y = y;
    }

    data->point = pts[0];
    data->state = LV_INDEV_STATE_PRESSED;

    input_gestures_update(pts, touches);

    uint8_t clear = 0;
    esp_err_t clear_err = gt911_write(0x814E, &clear, 1);
    if (clear_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear GT911 status: %s", esp_err_to_name(clear_err));
    }
}

void touch_gt911_init(void)
{
    esp_err_t err;
    bool driver_installed = false;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000
    };
    err = i2c_param_config(I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return;
    }

    err = i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return;
    }
    driver_installed = true;

    err = gpio_set_direction(GPIO_RST, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_set_direction (RST) failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    err = gpio_set_direction(GPIO_INT, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_set_direction (INT) failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    err = gpio_set_level(GPIO_RST, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_set_level (RST low) failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    err = gpio_set_level(GPIO_INT, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_set_level (INT low) failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    err = gpio_set_level(GPIO_RST, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_set_level (RST high) failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    err = gpio_set_direction(GPIO_INT, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_set_direction (INT input) failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    uint8_t product_id[4] = {0};
    err = gt911_read(0x8140, product_id, sizeof(product_id));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read GT911 product ID: %s", esp_err_to_name(err));
        goto cleanup;
    }

    bool all_zero = true;
    bool all_ff = true;
    for (size_t i = 0; i < sizeof(product_id); ++i) {
        if (product_id[i] != 0x00) {
            all_zero = false;
        }
        if (product_id[i] != 0xFF) {
            all_ff = false;
        }
    }

    if (all_zero || all_ff) {
        ESP_LOGE(TAG, "Invalid GT911 product ID response: %02X %02X %02X %02X",
                 product_id[0], product_id[1], product_id[2], product_id[3]);
        err = ESP_FAIL;
        goto cleanup;
    }

    char product_str[5];
    memcpy(product_str, product_id, sizeof(product_id));
    product_str[sizeof(product_id)] = '\0';
    for (size_t i = 0; i < sizeof(product_id); ++i) {
        if (!isprint((int)product_str[i])) {
            product_str[i] = '.';
        }
    }

    ESP_LOGI(TAG, "GT911 Product ID: %s", product_str);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = gt911_read_cb;
    indev = lv_indev_drv_register(&indev_drv);
    ESP_LOGI(TAG, "GT911 initialized");
    return;

cleanup:
    indev = NULL;
    if (driver_installed) {
        esp_err_t del_err = i2c_driver_delete(I2C_PORT);
        if (del_err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_driver_delete failed: %s", esp_err_to_name(del_err));
        }
    }
}
