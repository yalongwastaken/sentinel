/**
 * @file test_hcsr04_sensor.c
 * @author Anthony Yalong
 * @brief HC-SR04 ultrasonic sensor test application - validates sensor
 *        initialization, distance measurement, timeout handling, and range
 *        accuracy. Replace main/main.c with this file to run ultrasonic
 *        sensor tests.
 */

// imports
#include <math.h>
#include "hcsr04.h"
#include "main_hub_system_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

// test configuration
#define TASK_DELAY 100        // 100ms task delay
#define TASK_STACK_DEPTH 4096 // safe stack depth for testing
#define TASK_PRIORITY 5       // safe task priority for testing
#define LED_PIN GPIO_NUM_2
#define LED_ON_THRESHOLD_CM 30.0  // turn LED ON when closer than 30cm
#define LED_OFF_THRESHOLD_CM 35.0 // turn LED OFF when farther than 35cm
#define DISTANCE_CHANGE_THRESHOLD_CM 1.0

// logging
static const char *TAG = "test_hcsr04_sensor";

// hcsr04 task
static hcsr04_sensor_t hcsr04_sensor;
static TickType_t hcsr04_sensor_task_last_wake;
static float hcsr04_sensor_task_last_value;
static bool led_state;

// function prototypes
esp_err_t led_init(void);
esp_err_t led_update(float distance_cm);
void test_hcsr04_sensor(void *pvParameters);

void app_main(void)
{
    // error management
    esp_err_t ret;

    // initialize led
    led_state = false;
    ret = led_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize LED");
        return;
    }

    // initialize hcsr04
    ret = hcsr04_init(&hcsr04_sensor, HCSR04_TRIGGER_GPIO_PIN, HCSR04_ECHO_GPIO_PIN, HCSR04_ECHO_TIMEOUT_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize hcsr04");
        return;
    }

    // create task
    xTaskCreate(test_hcsr04_sensor, "test_hcsr04_sensor", TASK_STACK_DEPTH, NULL, TASK_PRIORITY, NULL);
}

esp_err_t led_init(void)
{
    // error management
    esp_err_t ret;

    // led config
    gpio_config_t led_config = {.pin_bit_mask = (1ULL << LED_PIN),
                                .mode = GPIO_MODE_OUTPUT,
                                .intr_type = GPIO_INTR_DISABLE,
                                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                .pull_up_en = GPIO_PULLUP_DISABLE};
    ret = gpio_config(&led_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure test led");
        return ret;
    }

    // initial state
    ret = gpio_set_level(LED_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set initial test led level");
        return ret;
    }

    ESP_LOGI(TAG, "test led initialized");
    return ESP_OK;
}

esp_err_t led_update(float distance_cm)
{
    // error management
    esp_err_t ret;

    if (distance_cm < LED_ON_THRESHOLD_CM) {
        // close - turn LED ON
        if (!led_state) {
            ret = gpio_set_level(LED_PIN, 1);
            if (ret != ESP_OK) {
                return ret;
            }
            led_state = true;

            // log led state change
            ESP_LOGI(TAG, "LED ON - object at %.2f cm", distance_cm);
        }
    } else if (distance_cm > LED_OFF_THRESHOLD_CM) {
        // far - turn LED OFF
        if (led_state) {
            ret = gpio_set_level(LED_PIN, 0);
            if (ret != ESP_OK) {
                return ret;
            }
            led_state = false;

            // log led state change
            ESP_LOGI(TAG, "LED OFF - object at %.2f cm", distance_cm);
        }
    }
    // between 30-35cm: do nothing (hysteresis band)
    return ESP_OK;
}

void test_hcsr04_sensor(void *pvParameters)
{
    // error management
    esp_err_t ret;

    // initialize task
    hcsr04_sensor_task_last_wake = xTaskGetTickCount();
    hcsr04_sensor_task_last_value = 0.0f;

    while (1) {
        // read sensor
        ret = hcsr04_measure_distance(&hcsr04_sensor);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "failed to read hcsr04 sensor distance");
            vTaskDelayUntil(&hcsr04_sensor_task_last_wake, pdMS_TO_TICKS(TASK_DELAY));
            continue;
        }

        // get distance
        float distance_cm = hcsr04_get_last_distance_cm(&hcsr04_sensor);

        // update LED
        led_update(distance_cm);

        // log distance
        if (fabs(distance_cm - hcsr04_sensor_task_last_value) >= DISTANCE_CHANGE_THRESHOLD_CM) {
            ESP_LOGI(TAG, "distance: %.2f cm", distance_cm);
            hcsr04_sensor_task_last_value = distance_cm;
        }

        // delay
        vTaskDelayUntil(&hcsr04_sensor_task_last_wake, pdMS_TO_TICKS(TASK_DELAY));
    }

    vTaskDelete(NULL);
}