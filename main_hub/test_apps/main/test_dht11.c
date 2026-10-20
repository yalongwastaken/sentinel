/**
 * @file test_dht11.c
 * @author Anthony Yalong
 * @brief DHT11 on the hub's wiring
 */

#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "dht11.h"
#include "main_hub_system_config.h"

// the datasheet's ranges
#define DHT11_MIN_TEMPERATURE_CELSIUS 0.0f
#define DHT11_MAX_TEMPERATURE_CELSIUS 50.0f
#define DHT11_MIN_RELATIVE_HUMIDITY_PERCENT 20.0f
#define DHT11_MAX_RELATIVE_HUMIDITY_PERCENT 90.0f
// an 18 ms start signal, an 80 us response, and 40 bits of at most 120 us each
#define DHT11_READ_BUDGET_US (18000 + 80 + 40 * 120)

// every case waits out the sensor's own interval, since the previous case may have just read it
static dht11_sensor_t init_dht11_and_wait_for_read_interval(void)
{
    dht11_sensor_t sensor;
    TEST_ASSERT_EQUAL(ESP_OK, dht11_init(&sensor, DHT11_GPIO_PIN));
    vTaskDelay(pdMS_TO_TICKS(DHT11_MIN_READ_INTERVAL_MS));
    return sensor;
}

TEST_CASE("a read lands inside the DHT11's datasheet ranges", "[dht11]")
{
    dht11_sensor_t sensor = init_dht11_and_wait_for_read_interval();
    dht11_reading_t reading;
    TEST_ASSERT_EQUAL(ESP_OK, dht11_read_temperature_and_humidity(&sensor, &reading));
    TEST_ASSERT_TRUE_MESSAGE(reading.temperature_celsius >= DHT11_MIN_TEMPERATURE_CELSIUS &&
                                 reading.temperature_celsius <= DHT11_MAX_TEMPERATURE_CELSIUS,
                             "temperature outside 0-50 C");
    TEST_ASSERT_TRUE_MESSAGE(reading.relative_humidity_percent >= DHT11_MIN_RELATIVE_HUMIDITY_PERCENT &&
                                 reading.relative_humidity_percent <= DHT11_MAX_RELATIVE_HUMIDITY_PERCENT,
                             "humidity outside 20-90 %");
}

TEST_CASE("a second read inside the minimum interval is refused", "[dht11]")
{
    dht11_sensor_t sensor = init_dht11_and_wait_for_read_interval();
    dht11_reading_t reading;
    TEST_ASSERT_EQUAL(ESP_OK, dht11_read_temperature_and_humidity(&sensor, &reading));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, dht11_read_temperature_and_humidity(&sensor, &reading));
}

TEST_CASE("a read finishes inside its time budget", "[dht11]")
{
    dht11_sensor_t sensor = init_dht11_and_wait_for_read_interval();
    dht11_reading_t reading;
    int64_t start_us = esp_timer_get_time();
    TEST_ASSERT_EQUAL(ESP_OK, dht11_read_temperature_and_humidity(&sensor, &reading));
    TEST_ASSERT_LESS_THAN_INT32(DHT11_READ_BUDGET_US, (int32_t)(esp_timer_get_time() - start_us));
}
