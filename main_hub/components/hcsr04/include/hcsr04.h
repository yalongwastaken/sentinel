/**
 * @file hcsr04.h
 * @author Anthony Yalong
 * @brief HC-SR04 ultrasonic distance sensor driver
 */
#ifndef HCSR04_H
#define HCSR04_H

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    gpio_num_t trigger_gpio_pin;
    gpio_num_t echo_gpio_pin;
    float last_distance_cm;
    uint32_t echo_timeout_us;
} hcsr04_sensor_t;

/**
 * @brief Configure TRIG as an output held low and ECHO as an input.
 *
 * @param echo_timeout_us How long to wait for each echo edge before giving up
 */
esp_err_t hcsr04_init(hcsr04_sensor_t *sensor, gpio_num_t trigger_gpio_pin, gpio_num_t echo_gpio_pin,
                      uint32_t echo_timeout_us);

/**
 * @brief Send a trigger pulse, time the echo, and store the distance.
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if an echo edge never came;
 *         the last stored distance is kept on timeout
 */
esp_err_t hcsr04_measure_distance(hcsr04_sensor_t *sensor);

/**
 * @brief Get the distance stored by the last successful measurement, 0 before the first.
 */
float hcsr04_get_last_distance_cm(const hcsr04_sensor_t *sensor);

#endif // HCSR04_H
