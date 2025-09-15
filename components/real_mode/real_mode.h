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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Structure décrivant le câblage matériel d'un terrarium */
typedef struct {
    /* Bus de communication */
    i2c_port_t i2c_port;           /* Bus I2C pour capteurs */
    gpio_num_t i2c_sda_gpio;       /* Broche SDA */
    gpio_num_t i2c_scl_gpio;       /* Broche SCL */
    spi_host_device_t spi_host;    /* Bus SPI optionnel */
    uart_port_t uart_port;         /* Bus UART pour MH-Z19B */
    gpio_num_t uart_tx_gpio;       /* TX vers MH-Z19B */
    gpio_num_t uart_rx_gpio;       /* RX depuis MH-Z19B */

    /* Adresses des capteurs */
    uint8_t sht31_addr;            /* Adresse I2C du SHT31 */
    uint8_t bh1750_addr;           /* Adresse I2C du BH1750 */

    /* Actionneurs */
    gpio_num_t heater_gpio;        /* Chauffage */
    gpio_num_t uv_gpio;            /* UV */
    gpio_num_t neon_gpio;          /* Néon */
    gpio_num_t pump_gpio;          /* Pompe */
    gpio_num_t fan_gpio;           /* Ventilation */
    gpio_num_t humidifier_gpio;    /* Humidificateur */

    /* Seuils de régulation */
    float temp_low_c;              /* Allumage chauffage */
    float temp_high_c;             /* Arrêt chauffage */
    float humidity_low_pct;        /* Allumage pompe/humidif. */
    float humidity_high_pct;       /* Arrêt pompe/humidif. */
    float lux_low_lx;              /* Allumage UV/Néon */
    float lux_high_lx;             /* Arrêt UV/Néon */
    float co2_high_ppm;            /* Allumage ventilation */
} terrarium_hw_t;

/* Données de capteurs */
typedef struct {
    float temperature_c;
    float humidity_pct;
    float luminosity_lux;
    float co2_ppm;
    float power_w;
} sensor_data_t;

typedef struct {
    bool temperature_humidity;
    bool luminosity;
    bool co2;
} sensor_connection_t;

typedef struct {
    bool heater;
    bool uv;
    bool neon;
    bool pump;
    bool fan;
    bool humidifier;
} actuator_connection_t;

typedef struct {
    sensor_connection_t sensors;
    actuator_connection_t actuators;
} terrarium_device_status_t;

/* Etat runtime d'un terrarium */
typedef struct {
    bool manual_mode; /* true : pilotage manuel */
    struct {
        bool heater;
        bool uv;
        bool neon;
        bool pump;
        bool fan;
        bool humidifier;
    } actuators;         /* Etats manuels des actionneurs */
} real_mode_state_t;

/* Configuration multi-terrariums */
extern terrarium_hw_t g_terrariums[];
extern const size_t g_terrarium_count;
extern real_mode_state_t g_real_mode_state[];
extern terrarium_device_status_t g_device_status[];

/* API principale */
void real_mode_init(void);
void real_mode_loop(void *arg);
void real_mode_detect_devices(void);

#ifdef __cplusplus
}
#endif

#endif /* REAL_MODE_H */
