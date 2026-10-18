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
#define DHT11_BIT_THRESHOLD_US 40

// ============================================================================
// Helper Function Prototypes
// ============================================================================
/**
 * @brief Wait for GPIO pin to reach specified level with timeout
 *
 * Polls the GPIO pin until it reaches the target level or timeout occurs.
 * Used for DHT11 protocol timing where the sensor controls the data line.
 *
 * @param pin GPIO pin to monitor
 * @param level Target level to wait for (0 = LOW, 1 = HIGH)
 * @param timeout_us Maximum time to wait in microseconds
 * @return esp_err_t ESP_OK if level reached, ESP_ERR_TIMEOUT otherwise
 */
static esp_err_t wait_for_level(gpio_num_t pin, bool level, uint32_t timeout_us);

/**
 * @brief Measure the width of a pulse on a GPIO pin
 *
 * Waits for pin to reach specified level, then measures duration until
 * pin returns to opposite level. Used to distinguish between DHT11's
 * short (~26-28μs) and long (~70μs) pulses for bit decoding.
 *
 * @param pin GPIO pin to monitor
 * @param level Level to measure (0 = LOW pulse, 1 = HIGH pulse)
 * @param timeout_us Maximum time to wait in microseconds
 * @return int64_t Pulse width in microseconds, or -1 on timeout
 */
static int64_t measure_pulse_width(gpio_num_t pin, uint32_t level, uint32_t timeout_us);

/**
 * @brief Read 40 bits of data from DHT11 sensor
 *
 * Reads 5 bytes (40 bits) of data after DHT11 response signal:
 * - Bytes 0-1: Humidity (integer + decimal)
 * - Bytes 2-3: Temperature (integer + decimal)
 * - Byte 4: Checksum
 * Decodes bits based on HIGH pulse width (>50μs = '1', <50μs = '0').
 *
 * @param sensor Pointer to initialized sensor structure
 * @return esp_err_t ESP_OK on success, ESP_ERR_TIMEOUT on read failure,
 *                   ESP_ERR_INVALID_CRC on checksum mismatch
 */
static esp_err_t read_data_bits(dht11_sensor_t *sensor);

// ============================================================================
// Public API Implementation
// ============================================================================
esp_err_t dht11_init(dht11_sensor_t *sensor, gpio_num_t pin)
{
    // error management
    esp_err_t ret;

    // sanity check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "dht11 sensor pointer is null");
        return ESP_ERR_INVALID_ARG;
    }

    // pin config
    gpio_config_t config = {
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pin_bit_mask = (1ULL << pin),
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ret = gpio_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize dht11 sensor's gpio pin");
        return ret;
    }

    // initialize state
    ret = gpio_set_level(pin, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set dht11 initial state");
        return ret;
    }
    sensor->pin = pin;
    sensor->last_temperature = 0.0;
    sensor->last_humidity = 0.0;
    sensor->last_read_time_us = 0;

    ESP_LOGI(TAG, "successfully initialized dht11 sensor");
    return ESP_OK;
}

esp_err_t dht11_read(dht11_sensor_t *sensor)
{
    // error management
    esp_err_t ret;

    // sanity check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "dht11 sensor pointer is null");
        return ESP_ERR_INVALID_ARG;
    }

    // setup
    gpio_num_t pin = sensor->pin;

    // check last read time - enforce minimum interval
    int64_t cur_time = esp_timer_get_time();
    if ((cur_time - sensor->last_read_time_us) < (DHT11_MIN_READ_INTERVAL_MS * 1000)) {
        ESP_LOGW(TAG, "read too soon, minimum interval is %d ms", DHT11_MIN_READ_INTERVAL_MS);
        return ESP_ERR_INVALID_STATE;
    }

    // pull data line LOW for 18ms (start signal)
    ret = gpio_set_direction(sensor->pin, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set pin direction to output");
        return ret;
    }

    ret = gpio_set_level(sensor->pin, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set pin level LOW");
        return ret;
    }

    esp_rom_delay_us(DHT11_START_SIGNAL_LOW_MS * 1000);

    // pull data line HIGH for 20-40μs
    ret = gpio_set_level(pin, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set pin level HIGH");
        return ret;
    }

    esp_rom_delay_us(30);

    // release line (switch to input for DHT11 to take control)
    ret = gpio_set_direction(pin, GPIO_MODE_INPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set pin direction to input");
        return ret;
    }

    // wait for DHT11 response signal (LOW)
    ret = wait_for_level(pin, 0, DHT11_RESPONSE_TIMEOUT_US);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "timeout waiting for DHT11 response LOW");
        return ret;
    }

    // wait for DHT11 ready signal (HIGH)
    ret = wait_for_level(pin, 1, DHT11_RESPONSE_TIMEOUT_US);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "timeout waiting for DHT11 response HIGH");
        return ret;
    }

    // read 40 bits of data
    ret = read_data_bits(sensor);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "failed to read data bits");
        return ret;
    }

    // update last read time on success
    sensor->last_read_time_us = esp_timer_get_time();

    return ESP_OK;
}

float dht11_get_temperature(const dht11_sensor_t *sensor)
{
    // sanity check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "dht11 sensor pointer is null");
        return 0.0;
    }

    return sensor->last_temperature;
}

float dht11_get_humidity(const dht11_sensor_t *sensor)
{
    // sanity check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "dht11 sensor pointer is null");
        return 0.0;
    }

    return sensor->last_humidity;
}

// ============================================================================
// Helper Function Implementation
// ============================================================================
static esp_err_t wait_for_level(gpio_num_t pin, bool level, uint32_t timeout_us)
{
    int64_t start_time = esp_timer_get_time();

    while (gpio_get_level(pin) != level) {
        if ((esp_timer_get_time() - start_time) > timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_OK;
}

static int64_t measure_pulse_width(gpio_num_t pin, uint32_t level, uint32_t timeout_us)
{
    // wait for pin to set to level
    int64_t start_time = esp_timer_get_time();
    while (gpio_get_level(pin) != level) {
        if ((esp_timer_get_time() - start_time) > timeout_us) {
            return -1; // timeout
        }
    }

    // Measure how long it stays at that level
    int64_t pulse_start = esp_timer_get_time();
    while (gpio_get_level(pin) == level) {
        if ((esp_timer_get_time() - pulse_start) > timeout_us) {
            return -1; // timeout
        }
    }

    return esp_timer_get_time() - pulse_start;
}

static esp_err_t read_data_bits(dht11_sensor_t *sensor)
{
    // error management
    esp_err_t ret;

    // read 40 bits
    uint8_t data[5] = {0};
    for (int i = 0; i < 40; i++) {
        int byte_index = i / 8;
        int bit_position = 7 - (i % 8);

        // wait for low (start of bit)
        ret = wait_for_level(sensor->pin, 0, DHT11_BIT_TIMEOUT_US);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Timeout waiting for bit %d LOW", i);
            return ESP_ERR_TIMEOUT;
        }

        // wait for high (bit value)
        ret = wait_for_level(sensor->pin, 1, DHT11_BIT_TIMEOUT_US);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Timeout waiting for bit %d HIGH", i);
            return ESP_ERR_TIMEOUT;
        }

        // measure HIGH pulse width
        int64_t pulse_width = measure_pulse_width(sensor->pin, 1, DHT11_BIT_TIMEOUT_US);
        if (pulse_width < 0) {
            ESP_LOGW(TAG, "timeout measuring pulse width for bit %d", i);
            return ESP_ERR_TIMEOUT;
        }

        // determine bit value
        if (pulse_width > DHT11_BIT_THRESHOLD_US) {
            // bit is 1
            data[byte_index] |= (1 << bit_position);
        }
    }

    // check sum
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        ESP_LOGW(TAG, "checksum mismatch: calculated 0x%02X, received 0x%02X", checksum, data[4]);
        return ESP_ERR_INVALID_CRC;
    }

    // extract values
    sensor->last_humidity = data[0];
    sensor->last_temperature = data[2];

    // log
    ESP_LOGD(TAG, "temp: %.1f°C, humidity: %.1f%%", sensor->last_temperature, sensor->last_humidity);

    return ESP_OK;
}