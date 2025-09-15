#include "real_mode.h"
#include "sensors.h"
#include "actuators.h"
#include "dashboard.h"
#include "logging.h"
#include "esp_log.h"

static const char *TAG = "real_mode";

/* Exemple de configuration pour deux terrariums */
terrarium_hw_t g_terrariums[] = {
    {
        .i2c_port = I2C_NUM_0,
        .i2c_sda_gpio = GPIO_NUM_18,
        .i2c_scl_gpio = GPIO_NUM_19,
        .spi_host = SPI2_HOST,
        .uart_port = UART_NUM_0,
        .uart_tx_gpio = GPIO_NUM_1,
        .uart_rx_gpio = GPIO_NUM_3,
        .sht31_addr = 0x44,
        .bh1750_addr = 0x23,
        .heater_gpio = GPIO_NUM_2,
        .uv_gpio = GPIO_NUM_3,
        .neon_gpio = GPIO_NUM_4,
        .pump_gpio = GPIO_NUM_5,
        .fan_gpio = GPIO_NUM_6,
        .humidifier_gpio = GPIO_NUM_7,
        .temp_low_c = 20.0f,
        .temp_high_c = 35.0f,
        .humidity_low_pct = 40.0f,
        .humidity_high_pct = 60.0f,
        .lux_low_lx = 50.0f,
        .lux_high_lx = 500.0f,
        .co2_high_ppm = 1500.0f
    },
    {
        .i2c_port = I2C_NUM_1,
        .i2c_sda_gpio = GPIO_NUM_25,
        .i2c_scl_gpio = GPIO_NUM_26,
        .spi_host = SPI3_HOST,
        .uart_port = UART_NUM_1,
        .uart_tx_gpio = GPIO_NUM_10,
        .uart_rx_gpio = GPIO_NUM_11,
        .sht31_addr = 0x45,
        .bh1750_addr = 0x5C,
        .heater_gpio = GPIO_NUM_8,
        .uv_gpio = GPIO_NUM_9,
        .neon_gpio = GPIO_NUM_10,
        .pump_gpio = GPIO_NUM_11,
        .fan_gpio = GPIO_NUM_12,
        .humidifier_gpio = GPIO_NUM_13,
        .temp_low_c = 22.0f,
        .temp_high_c = 32.0f,
        .humidity_low_pct = 35.0f,
        .humidity_high_pct = 55.0f,
        .lux_low_lx = 60.0f,
        .lux_high_lx = 400.0f,
        .co2_high_ppm = 1500.0f
    },
};
const size_t g_terrarium_count = sizeof(g_terrariums)/sizeof(g_terrariums[0]);
real_mode_state_t g_real_mode_state[sizeof(g_terrariums)/sizeof(g_terrariums[0])] = {0};

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
            esp_err_t sret = sensors_read(&g_terrariums[i], &data);
            if (sret == ESP_OK) {
                actuators_watchdog_feed(&g_terrariums[i]);
            }
            if (!g_real_mode_state[i].manual_mode && sret == ESP_OK) {
                actuators_apply(&g_terrariums[i], &data, &g_real_mode_state[i]);
                dashboard_update(&data);
                logging_write(i, &g_terrariums[i], &data);
            } else if (g_real_mode_state[i].manual_mode) {
                actuators_apply(&g_terrariums[i], NULL, &g_real_mode_state[i]);
                if (sret == ESP_OK) {
                    dashboard_update(&data);
                    logging_write(i, &g_terrariums[i], &data);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
