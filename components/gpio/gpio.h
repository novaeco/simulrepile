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

/* Pin Definitions */
#define LED_GPIO_PIN     GPIO_NUM_6  /* GPIO pin connected to the LED */
#define SERVO_FEED_PIN   GPIO_NUM_17 /* Servo control pin for feeding */
#define WATER_PUMP_PIN   GPIO_NUM_18 /* Pump control pin for watering */
#define HEAT_RES_PIN     GPIO_NUM_19 /* Heating resistor control pin */

/* Function Prototypes */

typedef struct {
    esp_err_t (*init)(void);
    void (*gpio_mode)(uint16_t pin, uint16_t mode);
    void (*gpio_int)(int32_t pin, gpio_isr_t isr_handler);
    void (*digital_write)(uint16_t pin, uint8_t value);
    uint8_t (*digital_read)(uint16_t pin);
    void (*feed)(void);
    void (*water)(void);
    void (*heat)(void);
    void (*deinit)(void);
} actuator_driver_t;

void DEV_GPIO_Mode(uint16_t Pin, uint16_t Mode);
void DEV_GPIO_INT(int32_t Pin, gpio_isr_t isr_handler);
void DEV_Digital_Write(uint16_t Pin, uint8_t Value);
uint8_t DEV_Digital_Read(uint16_t Pin);
void reptile_feed_gpio(void);
void reptile_water_gpio(void);
void reptile_heat_gpio(void);
esp_err_t reptile_actuators_init(void);
void reptile_actuators_deinit(void);

#endif
