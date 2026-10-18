/**
 * @file dht11.h
 * @author Anthony Yalong
 * @brief DHT11 temperature and humidity sensor driver
 */
#ifndef DHT11_H
#define DHT11_H

// imports
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

// configuration
#define DHT11_MIN_READ_INTERVAL_MS 2000 // minimum time between reads

/**
 * @brief DHT11 temperature & humidity sensor structure
 */
typedef struct {
    gpio_num_t pin;
    float last_temperature;
    float last_humidity;
    int64_t last_read_time_us;
} dht11_sensor_t;

/**
 * @brief Initialize DHT11 temperature & humidity sensor
 *
 * Configures the data pin for bidirectional communication with DHT11 sensor.
 * The pin will alternate between output (for start signal) and input (for reading data).
 *
 * @param sensor Pointer to sensor structure
 * @param pin GPIO pin number for DHT11 data line
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t dht11_init(dht11_sensor_t *sensor, gpio_num_t pin);

/**
 * @brief Read temperature and humidity from DHT11 sensor
 *
 * Performs a complete read cycle: sends start signal, reads 40 bits of data,
 * verifies checksum, and updates the sensor structure with new values.
 * Enforces minimum 2-second delay between consecutive reads per DHT11 spec.
 *
 * @param sensor Pointer to initialized sensor structure
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_STATE if read too soon,
 *                   ESP_ERR_TIMEOUT on communication timeout,
 *                   ESP_ERR_INVALID_CRC on checksum failure
 */
esp_err_t dht11_read(dht11_sensor_t *sensor);

/**
 * @brief Get last read temperature value
 *
 * Returns the temperature from the most recent successful read.
 * Call dht11_read() first to update the value.
 *
 * @param sensor Pointer to sensor structure
 * @return float Temperature in degrees Celsius (0-50°C range for DHT11)
 */
float dht11_get_temperature(const dht11_sensor_t *sensor);

/**
 * @brief Get last read humidity value
 *
 * Returns the relative humidity from the most recent successful read.
 * Call dht11_read() first to update the value.
 *
 * @param sensor Pointer to sensor structure
 * @return float Relative humidity as percentage (20-90% range for DHT11)
 */
float dht11_get_humidity(const dht11_sensor_t *sensor);

#endif // DHT11_H