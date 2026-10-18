/**
 * @file pir.h
 * @author Anthony Yalong
 * @brief PIR motion sensor driver for HC-SR501
 */

#ifndef PIR_H
#define PIR_H

// imports
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

// pir sensor structure
typedef struct {
    gpio_num_t pin_num;        // gpio pin number
    bool last_state;           // previous state for edge detection
    uint32_t motion_count;     // total motion events detected
    uint32_t debounce_ms;      // debounce time in milliseconds
    int64_t last_trigger_time; // last motion detection time (microseconds)
} pir_sensor_t;

/**
 * @brief Initialize PIR sensor on specified GPIO pin
 *
 * @param pir Pointer to PIR sensor structure
 * @param pin GPIO pin number for PIR sensor output
 * @param debounce_time Debounce interval in milliseconds to filter spurious or repeated motion triggers
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if pir is NULL
 */
esp_err_t pir_init(pir_sensor_t *pir, gpio_num_t pin, uint32_t debounce_time);

/**
 * @brief Read PIR sensor and detect motion events
 *
 * Reads current GPIO state and detects rising edges (LOW->HIGH).
 * Increments motion_count on each new motion detection.
 *
 * @param pir Pointer to PIR sensor structure
 * @return true if motion currently detected, false otherwise
 */
bool pir_read(pir_sensor_t *pir);

/**
 * @brief Get total number of motion events detected
 *
 * @param pir Pointer to PIR sensor structure
 * @return uint32_t Total motion events since initialization
 */
uint32_t pir_get_motion_count(const pir_sensor_t *pir);

/**
 * @brief Reset motion event counter to zero
 *
 * @param pir Pointer to PIR sensor structure
 */
void pir_reset_motion_count(pir_sensor_t *pir);

#endif // PIR_H