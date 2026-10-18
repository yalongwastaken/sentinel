/**
 * @file hcsr501.h
 * @author Anthony Yalong
 * @brief PIR motion sensor driver for HC-SR501
 */

#ifndef HCSR501_H
#define HCSR501_H

// imports
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

// sensor sensor structure
typedef struct {
    gpio_num_t gpio_pin;
    bool previous_level;         // for rising-edge detection
    uint32_t motion_event_count; // debounced rising edges since init or reset
    uint32_t debounce_ms;
    int64_t last_motion_event_us;
} hcsr501_sensor_t;

/**
 * @brief Initialize PIR sensor on specified GPIO pin
 *
 * @param sensor Pointer to HC-SR501 sensor structure
 * @param gpio_pin GPIO pin wired to the PIR sensor output
 * @param debounce_ms Minimum time between two counted motion events
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if sensor is NULL
 */
esp_err_t hcsr501_init(hcsr501_sensor_t *sensor, gpio_num_t gpio_pin, uint32_t debounce_ms);

/**
 * @brief Read the PIR output level and count debounced motion events
 *
 * Reads the current GPIO level. On a rising edge (LOW->HIGH) at least
 * debounce_ms after the last counted event, increments motion_event_count.
 *
 * @param sensor Pointer to HC-SR501 sensor structure
 * @return true if the PIR output is currently high, false otherwise
 */
bool hcsr501_read_level_and_count_motion_events(hcsr501_sensor_t *sensor);

/**
 * @brief Get the number of debounced motion events counted
 *
 * @param sensor Pointer to HC-SR501 sensor structure
 * @return uint32_t Motion events since initialization or the last reset
 */
uint32_t hcsr501_get_motion_event_count(const hcsr501_sensor_t *sensor);

/**
 * @brief Reset the motion event count to zero
 *
 * @param sensor Pointer to HC-SR501 sensor structure
 */
void hcsr501_reset_motion_event_count(hcsr501_sensor_t *sensor);

#endif // HCSR501_H
