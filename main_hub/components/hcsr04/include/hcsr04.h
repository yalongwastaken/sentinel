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
    gpio_num_t trig_pin;    // GPIO pin for trigger signal
    gpio_num_t echo_pin;    // GPIO pin for echo signal
    float last_distance_cm; // Last measured distance in cm
    uint32_t timeout_us;    // Echo timeout in microseconds
} hcsr04_sensor_t;

/**
 * @brief Initialize HC-SR04 ultrasonic sensor
 *
 * Configures trigger pin as output and echo pin as input.
 * Sets default timeout to 30ms (max range ~400cm).
 *
 * @param sensor Pointer to sensor structure
 * @param trig_pin GPIO pin number for trigger
 * @param echo_pin GPIO pin number for echo
 * @param timeout Timeout in microseconds to wait for echo pulse before aborting measurement
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t hcsr04_init(hcsr04_sensor_t *sensor, gpio_num_t trig_pin, gpio_num_t echo_pin, uint32_t timeout);

/**
 * @brief Read distance from ultrasonic sensor
 *
 * Sends trigger pulse and measures echo response time.
 * Stores result in last_distance_cm.
 *
 * @param sensor Pointer to sensor structure
 * @return esp_err_t ESP_OK on success, ESP_ERR_TIMEOUT if no echo
 */
esp_err_t hcsr04_read_distance(hcsr04_sensor_t *sensor);

/**
 * @brief Get last measured distance
 *
 * @param sensor Pointer to sensor structure
 * @return float Last distance in cm, or 0.0 if never measured
 */
float hcsr04_get_last_distance(const hcsr04_sensor_t *sensor);

#endif // HCSR04_H