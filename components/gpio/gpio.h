/*****************************************************************************
 * | File         :   gpio.h
 * | Author       :   Waveshare team
 * | Function     :   Hardware underlying interface
 * | Info         :
 * |                 GPIO driver code for hardware-level operations.
 * ----------------
 * | This version :   V1.0
 * | Date         :   2024-11-19
 * | Info         :   Basic version
 *
 ******************************************************************************/

#ifndef __GPIO_H
#define __GPIO_H

#include "driver/gpio.h"  // ESP-IDF GPIO driver library
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

/* Pin Definitions */
#define LED_GPIO_PIN     GPIO_NUM_6   /* GPIO pin connected to the LED */
#define SERVO_FEED_PIN   GPIO_NUM_NC  /* Servo feed driven via CH422G EXIO */
#define WATER_PUMP_PIN   GPIO_NUM_NC  /* Pump output handled by CH422G EXIO */
#define HEAT_RES_PIN     GPIO_NUM_NC  /* Heating output handled by CH422G EXIO */

#ifndef SERVO_FEED_EXIO
#define SERVO_FEED_EXIO  5            /* CH422G EXIO line energising the feeder */
#endif

#ifndef WATER_PUMP_EXIO
#define WATER_PUMP_EXIO  6            /* CH422G EXIO line controlling the pump */
#endif

#ifndef HEAT_RES_EXIO
#define HEAT_RES_EXIO    7            /* CH422G EXIO line controlling the heater */
#endif

#if SERVO_FEED_EXIO < 0 || SERVO_FEED_EXIO > 8
#error "SERVO_FEED_EXIO must be within 0..8 (0 disables the feeder output)"
#endif
#if WATER_PUMP_EXIO < 1 || WATER_PUMP_EXIO > 8
#error "WATER_PUMP_EXIO must be within 1..8"
#endif
#if HEAT_RES_EXIO < 1 || HEAT_RES_EXIO > 8
#error "HEAT_RES_EXIO must be within 1..8"
#endif
#if SERVO_FEED_EXIO > 0 && SERVO_FEED_EXIO == WATER_PUMP_EXIO
#error "SERVO_FEED_EXIO conflicts with WATER_PUMP_EXIO"
#endif
#if SERVO_FEED_EXIO > 0 && SERVO_FEED_EXIO == HEAT_RES_EXIO
#error "SERVO_FEED_EXIO conflicts with HEAT_RES_EXIO"
#endif
#if WATER_PUMP_EXIO == HEAT_RES_EXIO
#error "WATER_PUMP_EXIO conflicts with HEAT_RES_EXIO"
#endif

/* Default pulse widths (in milliseconds) applied to monostable actuators */
#define REPTILE_GPIO_HEAT_PULSE_MS 5000u
#define REPTILE_GPIO_PUMP_PULSE_MS 1000u

/* Function Prototypes */

typedef struct {
    esp_err_t (*init)(void);
    void (*gpio_mode)(gpio_num_t pin, gpio_mode_t mode);
    void (*gpio_int)(gpio_num_t pin, gpio_isr_t isr_handler);
    void (*digital_write)(gpio_num_t pin, uint8_t value);
    uint8_t (*digital_read)(gpio_num_t pin);
    void (*feed)(size_t channel);
    void (*water)(size_t channel);
    void (*heat)(size_t channel);
    void (*uv)(size_t channel, bool on);
    void (*deinit)(void);
    size_t channel_count;
} actuator_driver_t;

void DEV_GPIO_Mode(gpio_num_t Pin, gpio_mode_t Mode);
void DEV_GPIO_INT(gpio_num_t Pin, gpio_isr_t isr_handler);
void DEV_Digital_Write(gpio_num_t Pin, uint8_t Value);
uint8_t DEV_Digital_Read(gpio_num_t Pin);
void reptile_feed_gpio(void);
void reptile_water_gpio(void);
void reptile_heat_gpio(void);
void reptile_uv_gpio(bool on);
void reptile_feed_gpio_channel(size_t channel);
void reptile_water_gpio_channel(size_t channel);
void reptile_heat_gpio_channel(size_t channel);
void reptile_uv_gpio_channel(size_t channel, bool on);
size_t reptile_actuator_channel_count(void);
esp_err_t reptile_actuators_init(void);
void reptile_actuators_deinit(void);

#endif
