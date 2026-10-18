/**
 * @file dht11.c
 * @author Anthony Yalong
 * @brief DHT11 temperature and humidity sensor driver implementation
 */

#include "dht11.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

static const char *TAG = "DHT11";

// ============================================================================
// DHT11 Configuration
// ============================================================================
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

// ============================================================================
// Helper Function Prototypes
// ============================================================================
/**
 * @brief Wait for GPIO pin to reach specified level with timeout
 *
 * Polls the GPIO pin until it reaches the target level or timeout occurs.
 * Used for DHT11 protocol timing where the sensor controls the data line.
 *
 * @param gpio_pin GPIO pin to monitor
 * @param level Target level to wait for (0 = LOW, 1 = HIGH)
 * @param timeout_us Maximum time to wait in microseconds
 * @return esp_err_t ESP_OK if level reached, ESP_ERR_TIMEOUT otherwise
 */
static esp_err_t wait_for_gpio_level(gpio_num_t gpio_pin, bool level, uint32_t timeout_us);

/**
 * @brief Measure the width of a pulse on a GPIO pin
 *
 * Waits for pin to reach specified level, then measures duration until
 * pin returns to opposite level. Used to distinguish between DHT11's
 * short (~26-28μs) and long (~70μs) pulses for bit decoding.
 *
 * @param gpio_pin GPIO pin to monitor
 * @param level Level to measure (0 = LOW pulse, 1 = HIGH pulse)
 * @param timeout_us Maximum time to wait in microseconds
 * @return int64_t Pulse width in microseconds, or -1 on timeout
 */
static int64_t measure_pulse_width_us(gpio_num_t gpio_pin, uint32_t level, uint32_t timeout_us);

/**
 * @brief Read and verify the 40-bit frame the DHT11 sends after its response signal
 *
 * Frame layout, 5 bytes:
 * - Bytes 0-1: Humidity (integer + decimal)
 * - Bytes 2-3: Temperature (integer + decimal)
 * - Byte 4: Checksum
 * A bit is '1' when its HIGH pulse lasts longer than DHT11_ONE_BIT_MIN_HIGH_US.
 *
 * @param sensor Pointer to initialized sensor structure
 * @return esp_err_t ESP_OK on success, ESP_ERR_TIMEOUT on read failure,
 *                   ESP_ERR_INVALID_CRC on checksum mismatch
 */
static esp_err_t read_and_verify_frame(dht11_sensor_t *sensor);

// ============================================================================
// Public API Implementation
// ============================================================================
esp_err_t dht11_init(dht11_sensor_t *sensor, gpio_num_t gpio_pin)
{
    // error management
    esp_err_t err;

    // sanity check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "dht11 sensor pointer is null");
        return ESP_ERR_INVALID_ARG;
    }

    // pin config
    gpio_config_t gpio_settings = {
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pin_bit_mask = (1ULL << gpio_pin),
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    err = gpio_config(&gpio_settings);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize dht11 sensor's gpio pin");
        return err;
    }

    // initialize state
    err = gpio_set_level(gpio_pin, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set dht11 initial state");
        return err;
    }
    sensor->gpio_pin = gpio_pin;
    sensor->last_temperature_celsius = 0.0;
    sensor->last_relative_humidity_percent = 0.0;
    sensor->last_successful_read_us = 0;

    ESP_LOGI(TAG, "successfully initialized dht11 sensor");
    return ESP_OK;
}

esp_err_t dht11_read_temperature_and_humidity(dht11_sensor_t *sensor)
{
    // error management
    esp_err_t err;

    // sanity check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "dht11 sensor pointer is null");
        return ESP_ERR_INVALID_ARG;
    }

    // setup
    gpio_num_t gpio_pin = sensor->gpio_pin;

    // check last read time - enforce minimum interval
    int64_t now_us = esp_timer_get_time();
    if ((now_us - sensor->last_successful_read_us) < (DHT11_MIN_READ_INTERVAL_MS * 1000)) {
        ESP_LOGW(TAG, "read too soon, minimum interval is %d ms", DHT11_MIN_READ_INTERVAL_MS);
        return ESP_ERR_INVALID_STATE;
    }

    // pull data line LOW (start signal)
    err = gpio_set_direction(sensor->gpio_pin, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set pin direction to output");
        return err;
    }

    err = gpio_set_level(sensor->gpio_pin, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set pin level LOW");
        return err;
    }

    esp_rom_delay_us(DHT11_START_SIGNAL_LOW_MS * 1000);

    // pull data line HIGH
    err = gpio_set_level(gpio_pin, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set pin level HIGH");
        return err;
    }

    esp_rom_delay_us(DHT11_START_SIGNAL_HIGH_US);

    // release line (switch to input for DHT11 to take control)
    err = gpio_set_direction(gpio_pin, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set pin direction to input");
        return err;
    }

    // wait for DHT11 response signal (LOW)
    err = wait_for_gpio_level(gpio_pin, 0, DHT11_RESPONSE_TIMEOUT_US);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "timeout waiting for DHT11 response LOW");
        return err;
    }

    // wait for DHT11 ready signal (HIGH)
    err = wait_for_gpio_level(gpio_pin, 1, DHT11_RESPONSE_TIMEOUT_US);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "timeout waiting for DHT11 response HIGH");
        return err;
    }

    // read the frame
    err = read_and_verify_frame(sensor);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to read data bits");
        return err;
    }

    // update last read time on success
    sensor->last_successful_read_us = esp_timer_get_time();

    return ESP_OK;
}

float dht11_get_last_temperature_celsius(const dht11_sensor_t *sensor)
{
    // sanity check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "dht11 sensor pointer is null");
        return 0.0;
    }

    return sensor->last_temperature_celsius;
}

float dht11_get_last_relative_humidity_percent(const dht11_sensor_t *sensor)
{
    // sanity check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "dht11 sensor pointer is null");
        return 0.0;
    }

    return sensor->last_relative_humidity_percent;
}

// ============================================================================
// Helper Function Implementation
// ============================================================================
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
    // wait for pin to set to level
    int64_t wait_start_us = esp_timer_get_time();
    while (gpio_get_level(gpio_pin) != level) {
        if ((esp_timer_get_time() - wait_start_us) > timeout_us) {
            return -1; // timeout
        }
    }

    // Measure how long it stays at that level
    int64_t pulse_start_us = esp_timer_get_time();
    while (gpio_get_level(gpio_pin) == level) {
        if ((esp_timer_get_time() - pulse_start_us) > timeout_us) {
            return -1; // timeout
        }
    }

    return esp_timer_get_time() - pulse_start_us;
}

static esp_err_t read_and_verify_frame(dht11_sensor_t *sensor)
{
    // error management
    esp_err_t err;

    // read the frame
    uint8_t frame[DHT11_FRAME_BYTES] = {0};
    for (int bit_index = 0; bit_index < DHT11_FRAME_BITS; bit_index++) {
        int byte_index = bit_index / 8;
        int bit_position = 7 - (bit_index % 8);

        // wait for low (start of bit)
        err = wait_for_gpio_level(sensor->gpio_pin, 0, DHT11_BIT_TIMEOUT_US);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Timeout waiting for bit %d LOW", bit_index);
            return ESP_ERR_TIMEOUT;
        }

        // wait for high (bit value)
        err = wait_for_gpio_level(sensor->gpio_pin, 1, DHT11_BIT_TIMEOUT_US);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Timeout waiting for bit %d HIGH", bit_index);
            return ESP_ERR_TIMEOUT;
        }

        // measure HIGH pulse width
        int64_t high_pulse_us = measure_pulse_width_us(sensor->gpio_pin, 1, DHT11_BIT_TIMEOUT_US);
        if (high_pulse_us < 0) {
            ESP_LOGW(TAG, "timeout measuring pulse width for bit %d", bit_index);
            return ESP_ERR_TIMEOUT;
        }

        // determine bit value
        if (high_pulse_us > DHT11_ONE_BIT_MIN_HIGH_US) {
            // bit is 1
            frame[byte_index] |= (1 << bit_position);
        }
    }

    // check sum
    uint8_t computed_checksum = frame[0] + frame[1] + frame[2] + frame[3];
    if (computed_checksum != frame[DHT11_CHECKSUM_BYTE]) {
        ESP_LOGW(TAG, "checksum mismatch: calculated 0x%02X, received 0x%02X", computed_checksum,
                 frame[DHT11_CHECKSUM_BYTE]);
        return ESP_ERR_INVALID_CRC;
    }

    // extract values
    sensor->last_relative_humidity_percent = frame[DHT11_HUMIDITY_INTEGER_BYTE];
    sensor->last_temperature_celsius = frame[DHT11_TEMPERATURE_INTEGER_BYTE];

    // log
    ESP_LOGD(TAG, "temp: %.1f°C, humidity: %.1f%%", sensor->last_temperature_celsius,
             sensor->last_relative_humidity_percent);

    return ESP_OK;
}
