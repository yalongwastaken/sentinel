/**
 * @file test_dht11_sensor.c
 * @author Anthony Yalong
 * @brief DHT11 temperature and humidity sensor test application - validates
 *        sensor initialization, data reading, checksum verification, and
 *        environmental measurements. Replace main/main.c with this file.
 */

// imports
#include "dht11.h"
#include "main_hub_system_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// test configuration
#define TASK_DELAY 3000       // 3s task delay
#define TASK_STACK_DEPTH 4096 // safe stack depth for testing
#define TASK_PRIORITY 5       // safe task priority for testing

// logging
static const char *TAG = "test_dht11_sensor";

// task parameters
static dht11_sensor_t sensor;
static TickType_t dht11_sensor_task_last_wake;

// function prototypes
void test_dht11_sensor(void *pvParameters);

void app_main(void)
{
    // error management
    esp_err_t ret;

    // initialize dht11 sensor
    ret = dht11_init(&sensor, DHT11_GPIO_PIN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize dht11 sensor");
        return;
    }

    // create task
    xTaskCreate(test_dht11_sensor, "test_dht11_sensor", TASK_STACK_DEPTH, NULL, TASK_PRIORITY, NULL);
}

void test_dht11_sensor(void *pvParameters)
{
    // error management
    esp_err_t ret;

    // initialize timing
    dht11_sensor_task_last_wake = xTaskGetTickCount();

    while (true) {
        // read
        ret = dht11_read_temperature_and_humidity(&sensor);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "failed to read dht11 sensor");
            vTaskDelayUntil(&dht11_sensor_task_last_wake, pdMS_TO_TICKS(TASK_DELAY));
            continue;
        }

        // log results
        float temp = dht11_get_last_temperature_celsius(&sensor);
        float humidity = dht11_get_last_relative_humidity_percent(&sensor);
        ESP_LOGI(TAG, "Temp: %.1f°C, Humidity: %.1f%%", temp, humidity);

        // delay
        vTaskDelayUntil(&dht11_sensor_task_last_wake, pdMS_TO_TICKS(TASK_DELAY));
    }

    // safety
    vTaskDelete(NULL);
}