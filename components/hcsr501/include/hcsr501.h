/**
 * @file hcsr501.h
 * @author Anthony Yalong
 * @brief HC-SR501 PIR motion sensor driver
 */

#ifndef HCSR501_H
#define HCSR501_H

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    gpio_num_t gpio_pin;
} hcsr501_sensor_t;

/**
 * @brief Configure the GPIO pin wired to the sensor output as an input.
 */
esp_err_t hcsr501_init(hcsr501_sensor_t *sensor, gpio_num_t gpio_pin);

/**
 * @brief Report whether the sensor output is high, meaning it sees motion.
 *
 * The module holds its output high for its own potentiometer-set delay, so the
 * level needs no debouncing here.
 */
bool hcsr501_is_motion_detected(const hcsr501_sensor_t *sensor);

#endif // HCSR501_H
