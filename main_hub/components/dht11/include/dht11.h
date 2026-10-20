/**
 * @file dht11.h
 * @author Anthony Yalong
 * @brief DHT11 temperature and humidity sensor driver
 */
#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

// the datasheet's minimum time between two reads
#define DHT11_MIN_READ_INTERVAL_MS 2000

typedef struct {
    float temperature_celsius;
    float relative_humidity_percent;
} dht11_reading_t;

typedef struct {
    gpio_num_t gpio_pin;
    int64_t last_successful_read_us;
} dht11_sensor_t;

/**
 * @brief Configure the single data pin, which the driver drives for the start
 *        signal and then releases for the sensor's reply, and hold it high.
 */
esp_err_t dht11_init(dht11_sensor_t *sensor, gpio_num_t gpio_pin);

/**
 * @brief Send the start signal, read the 40-bit frame, verify its checksum,
 *        and report temperature and humidity.
 *
 * @return ESP_OK with reading set;
 *         ESP_ERR_INVALID_STATE if called sooner than DHT11_MIN_READ_INTERVAL_MS
 *         after the last successful read;
 *         ESP_ERR_TIMEOUT if the sensor stops answering mid-frame;
 *         ESP_ERR_INVALID_CRC if the checksum does not match.
 *         reading is untouched on every failure.
 */
esp_err_t dht11_read_temperature_and_humidity(dht11_sensor_t *sensor, dht11_reading_t *reading);

#endif // DHT11_H
