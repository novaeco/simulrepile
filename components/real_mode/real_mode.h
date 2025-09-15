#ifndef REAL_MODE_H
#define REAL_MODE_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Structure décrivant le câblage matériel d'un terrarium */
typedef struct {
    i2c_port_t i2c_port;           /* Bus I2C pour capteurs */
    spi_host_device_t spi_host;    /* Bus SPI optionnel */
    uart_port_t uart_port;         /* Bus UART optionnel */
    gpio_num_t heater_gpio;        /* Chauffage */
    gpio_num_t uv_gpio;            /* UV */
    gpio_num_t neon_gpio;          /* Néon */
    gpio_num_t pump_gpio;          /* Pompe */
    gpio_num_t fan_gpio;           /* Ventilation */
    gpio_num_t humidifier_gpio;    /* Humidificateur */
} terrarium_hw_t;

/* Données de capteurs */
typedef struct {
    float temperature_c;
    float humidity_pct;
    float luminosity_lux;
    float co2_ppm;
} sensor_data_t;

/* Configuration multi-terrariums */
extern terrarium_hw_t g_terrariums[];
extern const size_t g_terrarium_count;

/* API principale */
void real_mode_init(void);
void real_mode_loop(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* REAL_MODE_H */
