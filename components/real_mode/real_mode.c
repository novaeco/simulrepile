#include "real_mode.h"
#include "sensors.h"
#include "actuators.h"
#include "dashboard.h"
#include "logging.h"
#include "esp_log.h"

static const char *TAG = "real_mode";

/* Exemple de configuration pour deux terrariums */
terrarium_hw_t g_terrariums[] = {
    { .i2c_port = I2C_NUM_0, .spi_host = SPI2_HOST, .uart_port = UART_NUM_0,
      .heater_gpio = GPIO_NUM_2, .uv_gpio = GPIO_NUM_3, .neon_gpio = GPIO_NUM_4,
      .pump_gpio = GPIO_NUM_5, .fan_gpio = GPIO_NUM_6, .humidifier_gpio = GPIO_NUM_7 },
    { .i2c_port = I2C_NUM_1, .spi_host = SPI3_HOST, .uart_port = UART_NUM_1,
      .heater_gpio = GPIO_NUM_8, .uv_gpio = GPIO_NUM_9, .neon_gpio = GPIO_NUM_10,
      .pump_gpio = GPIO_NUM_11, .fan_gpio = GPIO_NUM_12, .humidifier_gpio = GPIO_NUM_13 },
};
const size_t g_terrarium_count = sizeof(g_terrariums)/sizeof(g_terrariums[0]);

void real_mode_init(void)
{
    ESP_LOGI(TAG, "Initialisation du mode réel (%d modules)", (int)g_terrarium_count);
    for (size_t i = 0; i < g_terrarium_count; ++i) {
        sensors_init(&g_terrariums[i]);
        actuators_init(&g_terrariums[i]);
    }
    logging_init();
    dashboard_init();
    dashboard_show();
}

void real_mode_loop(void *arg)
{
    (void)arg;
    sensor_data_t data;
    while (1) {
        for (size_t i = 0; i < g_terrarium_count; ++i) {
            if (sensors_read(&g_terrariums[i], &data) == ESP_OK) {
                actuators_apply(&g_terrariums[i], &data);
                dashboard_update(&data);
                logging_write(&data);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
