/**
 * @file hcsr501.h
 * @author Anthony Yalong
 * @brief HC-SR501 PIR motion sensor driver
 */

#ifndef HCSR501_H
#define HCSR501_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    gpio_num_t gpio_pin;
    bool previous_level;
    uint32_t motion_event_count;
    uint32_t debounce_ms;
    int64_t last_motion_event_us;
} hcsr501_sensor_t;

/**
 * @brief Configure the GPIO pin wired to the sensor output as an input.
 *
 * @param debounce_ms Minimum time between two counted motion events
 */
esp_err_t hcsr501_init(hcsr501_sensor_t *sensor, gpio_num_t gpio_pin, uint32_t debounce_ms);

/**
 * @brief Read the sensor output and count debounced motion events.
 *
 * A rising edge at least debounce_ms after the last counted event increments
 * the motion event count. The debounce affects only the count, never the
 * returned level.
 *
 * @return true while the sensor output is high
 */
bool hcsr501_read_level_and_count_motion_events(hcsr501_sensor_t *sensor);

/**
 * @brief Get the number of debounced motion events since init or the last reset.
 */
uint32_t hcsr501_get_motion_event_count(const hcsr501_sensor_t *sensor);

void hcsr501_reset_motion_event_count(hcsr501_sensor_t *sensor);

#endif // HCSR501_H
