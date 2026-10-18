/**
 * @file dht11.c
 * @author Anthony Yalong
 * @brief DHT11 temperature and humidity sensor driver implementation
 */

#include <stdbool.h>
#include "dht11.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

static const char *TAG = "DHT11";

#define DHT11_START_SIGNAL_LOW_MS 18
#define DHT11_START_SIGNAL_HIGH_US 30
#define DHT11_RESPONSE_TIMEOUT_US 100
#define DHT11_BIT_TIMEOUT_US 200
#define DHT11_ONE_BIT_MIN_HIGH_US 40
#define DHT11_FRAME_BITS 40
#define DHT11_FRAME_BYTES 5
#define DHT11_HUMIDITY_INTEGER_BYTE 0
#define DHT11_TEMPERATURE_INTEGER_BYTE 2
#define DHT11_CHECKSUM_BYTE 4

static esp_err_t wait_for_gpio_level(gpio_num_t gpio_pin, bool level, uint32_t timeout_us);

/**
 * @brief Wait for the pin to reach level, then time how long it stays there.
 *
 * @return the pulse width in microseconds, or -1 if either wait exceeds timeout_us
 */
static int64_t measure_pulse_width_us(gpio_num_t gpio_pin, uint32_t level, uint32_t timeout_us);

/**
 * @brief Read and verify the frame the DHT11 sends after its response signal.
 *
 * Bytes 0-1 are humidity (integer, decimal), bytes 2-3 temperature (integer,
 * decimal), byte 4 the checksum. A bit is '1' when its high pulse lasts longer
 * than DHT11_ONE_BIT_MIN_HIGH_US; the datasheet's pulses are about 26-28 us for
 * '0' and about 70 us for '1'.
 */
static esp_err_t read_and_verify_frame(dht11_sensor_t *sensor);

esp_err_t dht11_init(dht11_sensor_t *sensor, gpio_num_t gpio_pin)
{
    gpio_config_t gpio_settings = {
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pin_bit_mask = (1ULL << gpio_pin),
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    esp_err_t err = gpio_config(&gpio_settings);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure gpio %d", gpio_pin);
        return err;
    }

    err = gpio_set_level(gpio_pin, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to hold gpio %d high", gpio_pin);
        return err;
    }
    sensor->gpio_pin = gpio_pin;
    sensor->last_temperature_celsius = 0.0;
    sensor->last_relative_humidity_percent = 0.0;
    sensor->last_successful_read_us = 0;

    ESP_LOGI(TAG, "initialized on gpio %d", gpio_pin);
    return ESP_OK;
}

esp_err_t dht11_read_temperature_and_humidity(dht11_sensor_t *sensor)
{
    int64_t now_us = esp_timer_get_time();
    if ((now_us - sensor->last_successful_read_us) < (DHT11_MIN_READ_INTERVAL_MS * 1000)) {
        ESP_LOGW(TAG, "read too soon, minimum interval is %d ms", DHT11_MIN_READ_INTERVAL_MS);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = gpio_set_direction(sensor->gpio_pin, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to drive the data line");
        return err;
    }

    err = gpio_set_level(sensor->gpio_pin, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to pull the data line low");
        return err;
    }
    esp_rom_delay_us(DHT11_START_SIGNAL_LOW_MS * 1000);

    err = gpio_set_level(sensor->gpio_pin, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to pull the data line high");
        return err;
    }
    esp_rom_delay_us(DHT11_START_SIGNAL_HIGH_US);

    err = gpio_set_direction(sensor->gpio_pin, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to release the data line");
        return err;
    }

    err = wait_for_gpio_level(sensor->gpio_pin, 0, DHT11_RESPONSE_TIMEOUT_US);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "timeout waiting for the response to go low");
        return err;
    }

    err = wait_for_gpio_level(sensor->gpio_pin, 1, DHT11_RESPONSE_TIMEOUT_US);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "timeout waiting for the response to go high");
        return err;
    }

    err = read_and_verify_frame(sensor);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to read the frame");
        return err;
    }

    sensor->last_successful_read_us = esp_timer_get_time();
    return ESP_OK;
}

float dht11_get_last_temperature_celsius(const dht11_sensor_t *sensor) { return sensor->last_temperature_celsius; }

float dht11_get_last_relative_humidity_percent(const dht11_sensor_t *sensor)
{
    return sensor->last_relative_humidity_percent;
}

static esp_err_t wait_for_gpio_level(gpio_num_t gpio_pin, bool level, uint32_t timeout_us)
{
    int64_t wait_start_us = esp_timer_get_time();
    while (gpio_get_level(gpio_pin) != level) {
        if ((esp_timer_get_time() - wait_start_us) > timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

static int64_t measure_pulse_width_us(gpio_num_t gpio_pin, uint32_t level, uint32_t timeout_us)
{
    int64_t wait_start_us = esp_timer_get_time();
    while (gpio_get_level(gpio_pin) != level) {
        if ((esp_timer_get_time() - wait_start_us) > timeout_us) {
            return -1;
        }
    }

    int64_t pulse_start_us = esp_timer_get_time();
    while (gpio_get_level(gpio_pin) == level) {
        if ((esp_timer_get_time() - pulse_start_us) > timeout_us) {
            return -1;
        }
    }
    return esp_timer_get_time() - pulse_start_us;
}

static esp_err_t read_and_verify_frame(dht11_sensor_t *sensor)
{
    uint8_t frame[DHT11_FRAME_BYTES] = {0};
    for (int bit_index = 0; bit_index < DHT11_FRAME_BITS; bit_index++) {
        int byte_index = bit_index / 8;
        int bit_position = 7 - (bit_index % 8);

        if (wait_for_gpio_level(sensor->gpio_pin, 0, DHT11_BIT_TIMEOUT_US) != ESP_OK) {
            ESP_LOGW(TAG, "timeout waiting for bit %d to start", bit_index);
            return ESP_ERR_TIMEOUT;
        }
        if (wait_for_gpio_level(sensor->gpio_pin, 1, DHT11_BIT_TIMEOUT_US) != ESP_OK) {
            ESP_LOGW(TAG, "timeout waiting for bit %d to go high", bit_index);
            return ESP_ERR_TIMEOUT;
        }

        int64_t high_pulse_us = measure_pulse_width_us(sensor->gpio_pin, 1, DHT11_BIT_TIMEOUT_US);
        if (high_pulse_us < 0) {
            ESP_LOGW(TAG, "timeout measuring bit %d", bit_index);
            return ESP_ERR_TIMEOUT;
        }

        if (high_pulse_us > DHT11_ONE_BIT_MIN_HIGH_US) {
            frame[byte_index] |= (1 << bit_position);
        }
    }

    uint8_t computed_checksum = frame[0] + frame[1] + frame[2] + frame[3];
    if (computed_checksum != frame[DHT11_CHECKSUM_BYTE]) {
        ESP_LOGW(TAG, "checksum mismatch: computed 0x%02X, received 0x%02X", computed_checksum,
                 frame[DHT11_CHECKSUM_BYTE]);
        return ESP_ERR_INVALID_CRC;
    }

    sensor->last_relative_humidity_percent = frame[DHT11_HUMIDITY_INTEGER_BYTE];
    sensor->last_temperature_celsius = frame[DHT11_TEMPERATURE_INTEGER_BYTE];
    ESP_LOGD(TAG, "temperature: %.1f C, humidity: %.1f %%", sensor->last_temperature_celsius,
             sensor->last_relative_humidity_percent);

    return ESP_OK;
}
