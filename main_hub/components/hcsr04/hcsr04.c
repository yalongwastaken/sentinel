/**
 * @file hcsr04.c
 * @author Anthony Yalong
 * @brief Ultrasonic sensor driver implementation
 */

#include "hcsr04.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

static const char *TAG = "HCSR04";

esp_err_t hcsr04_init(hcsr04_sensor_t *sensor, gpio_num_t trig_pin, gpio_num_t echo_pin, uint32_t timeout)
{
    // error management
    esp_err_t ret;

    // sanity check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "hcsr04 sensor pointer is null");
        return ESP_ERR_INVALID_ARG;
    }

    // trig config
    gpio_config_t trig_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << trig_pin),
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ret = gpio_config(&trig_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize hcsr04 sensor's trigger pin");
        return ret;
    }

    // echo config
    gpio_config_t echo_config = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << echo_pin),
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ret = gpio_config(&echo_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize hcsr04 sensor's echo pin");
        return ret;
    }

    // intialize state
    gpio_set_level(trig_pin, 0);
    sensor->trig_pin = trig_pin;
    sensor->echo_pin = echo_pin;
    sensor->last_distance_cm = 0.0;
    sensor->timeout_us = timeout;

    ESP_LOGI(TAG, "successfully initialized hcsr04 sensor");
    return ESP_OK;
}

esp_err_t hcsr04_read_distance(hcsr04_sensor_t *sensor)
{
    // sanity check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "hcsr04 sensor pointer is null");
        return ESP_ERR_INVALID_ARG;
    }

    // send 10us trigger pulse
    gpio_set_level(sensor->trig_pin, 0);
    esp_rom_delay_us(2);
    gpio_set_level(sensor->trig_pin, 1);
    esp_rom_delay_us(10);
    gpio_set_level(sensor->trig_pin, 0);

    // wait for ECHO to go HIGH (with timeout)
    int64_t start_time = esp_timer_get_time();
    while (gpio_get_level(sensor->echo_pin) == 0) {
        if ((esp_timer_get_time() - start_time) > sensor->timeout_us) {
            ESP_LOGW(TAG, "timeout waiting for ECHO HIGH");
            return ESP_ERR_TIMEOUT;
        }
    }

    // record when ECHO went HIGH
    int64_t echo_start = esp_timer_get_time();

    // wait for ECHO to go LOW (with timeout)
    while (gpio_get_level(sensor->echo_pin) == 1) {
        if ((esp_timer_get_time() - echo_start) > sensor->timeout_us) {
            ESP_LOGW(TAG, "timeout waiting for ECHO LOW");
            return ESP_ERR_TIMEOUT;
        }
    }

    // record when ECHO went LOW
    int64_t echo_end = esp_timer_get_time();

    // calculate pulse width
    int64_t pulse_width = echo_end - echo_start;

    // calculate distance: speed of sound = 343 m/s = 0.0343 cm/μs
    // distance = (pulse_width * 0.0343) / 2
    sensor->last_distance_cm = (pulse_width * 0.034) / 2.0;
    ESP_LOGD(TAG, "distance: %.2f cm (pulse: %lld us)", sensor->last_distance_cm, pulse_width);

    ESP_LOGI(TAG, "successfully read distance of hcsr04 sensor: %0.02f", sensor->last_distance_cm);
    return ESP_OK;
}

float hcsr04_get_last_distance(const hcsr04_sensor_t *sensor)
{
    // sanity check
    if (sensor == NULL) {
        ESP_LOGW(TAG, "hcsr04 pointer is null");
        return 0.0;
    }
    return sensor->last_distance_cm;
}