/**
 * @file hcsr04.h
 * @author Anthony Yalong
 * @brief Ultrasonic sensor driver for HC-SR04
 */
#ifndef HCSR04_H
#define HCSR04_H

// imports
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

/**
 * @brief HC-SR04 ultrasonic sensor structure
 */
typedef struct {
    gpio_num_t trigger_gpio_pin;
    gpio_num_t echo_gpio_pin;
    float last_distance_cm;
    uint32_t echo_timeout_us;
} hcsr04_sensor_t;

/**
 * @brief Initialize HC-SR04 ultrasonic sensor
 *
 * Configures trigger pin as output and echo pin as input.
 *
 * @param sensor Pointer to sensor structure
 * @param trigger_gpio_pin GPIO pin wired to TRIG
 * @param echo_gpio_pin GPIO pin wired to ECHO
 * @param echo_timeout_us How long to wait for each echo edge before aborting a measurement
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t hcsr04_init(hcsr04_sensor_t *sensor, gpio_num_t trigger_gpio_pin, gpio_num_t echo_gpio_pin,
                      uint32_t echo_timeout_us);

/**
 * @brief Measure distance with the ultrasonic sensor
 *
 * Sends a trigger pulse, times the echo, and stores the result in
 * last_distance_cm.
 *
 * @param sensor Pointer to sensor structure
 * @return esp_err_t ESP_OK on success, ESP_ERR_TIMEOUT if no echo
 */
esp_err_t hcsr04_measure_distance(hcsr04_sensor_t *sensor);

/**
 * @brief Get last measured distance
 *
 * @param sensor Pointer to sensor structure
 * @return float Last distance in cm, or 0.0 if never measured
 */
float hcsr04_get_last_distance_cm(const hcsr04_sensor_t *sensor);

#endif // HCSR04_H
