/**
 * @file test_pir_sensor.c
 * @author Anthony Yalong
 * @brief PIR motion sensor test application - validates sensor initialization,
 *        motion detection, edge detection, debouncing, and event counting.
 *        Replace main/main.c with this file to run PIR sensor tests.
 */

// imports
#include "pir.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "remote_node_system_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// test configuration
#define TASK_DELAY 100      // 100ms task delay
#define TEST_LED GPIO_NUM_2 // built in LED
#define PIR_MOTION_COUNT_MAX 10
#define TASK_STACK_DEPTH 4096 // safe stack depth for testing
#define TASK_PRIORITY 5       // safe task priority for testing

// logging
static const char *TAG = "test_pir_sensor";

// pir task
static pir_sensor_t pir_sensor;
static TickType_t pir_sensor_task_last_wake;
static uint32_t pir_sensor_last_motion_count;
static bool pir_sensor_last_value;

// function prototypes
void test_pir_sensor(void *pvParameters);
esp_err_t led_init(void);

void app_main(void)
{
    // error management
    esp_err_t ret;

    // initialize test LED
    ret = led_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize test led");
        return;
    }

    // initialize task
    pir_sensor_task_last_wake = xTaskGetTickCount();
    pir_sensor_last_motion_count = 0;
    pir_sensor_last_value = false;

    // initalize pir
    ret = pir_init(&pir_sensor, PIR_GPIO_PIN, PIR_DEBOUNCE_TIME_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize pir sensor");
        return;
    }

    // create task
    xTaskCreate(test_pir_sensor, "test_pir_sensor", TASK_STACK_DEPTH, NULL, TASK_PRIORITY, NULL);
}

void test_pir_sensor(void *pvParameters)
{
    while (true) {
        // read sensor
        bool cur_reading = pir_read(&pir_sensor);
        if (pir_sensor_last_value != cur_reading) {
            ESP_LOGI(TAG, "led %s", cur_reading ? "ON" : "OFF");
            gpio_set_level(TEST_LED, cur_reading);
            pir_sensor_last_value = cur_reading;
        }

        // motion count update
        uint32_t pir_sensor_motion_count = pir_get_motion_count(&pir_sensor);
        if (pir_sensor_last_motion_count != pir_sensor_motion_count) {
            ESP_LOGI(TAG, "motion count %lu", pir_sensor_motion_count);
            pir_sensor_last_motion_count = pir_sensor_motion_count;
        }

        // motion count reset
        if (pir_sensor_motion_count == PIR_MOTION_COUNT_MAX) {
            ESP_LOGI(TAG, "motion count at max value: %lu.  reseting to 0...", pir_sensor_motion_count);
            pir_reset_motion_count(&pir_sensor);
            pir_sensor_last_motion_count = 0;
        }

        vTaskDelayUntil(&pir_sensor_task_last_wake, pdMS_TO_TICKS(TASK_DELAY));
    }

    vTaskDelete(NULL);
}

esp_err_t led_init(void)
{
    // error management
    esp_err_t ret;

    // led config
    gpio_config_t led_config = {.pin_bit_mask = (1ULL << TEST_LED),
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
    ret = gpio_set_level(TEST_LED, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set initial test led level");
        return ret;
    }

    ESP_LOGI(TAG, "test led initialized");
    return ESP_OK;
}