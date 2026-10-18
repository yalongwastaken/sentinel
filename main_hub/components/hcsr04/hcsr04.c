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

#define HCSR04_TRIGGER_SETTLE_LOW_US 2
#define HCSR04_TRIGGER_PULSE_US 10
#define HCSR04_SPEED_OF_SOUND_CM_PER_US 0.034

esp_err_t hcsr04_init(hcsr04_sensor_t *sensor, gpio_num_t trigger_gpio_pin, gpio_num_t echo_gpio_pin,
                      uint32_t echo_timeout_us)
{
    // error management
    esp_err_t err;

    // sanity check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "hcsr04 sensor pointer is null");
        return ESP_ERR_INVALID_ARG;
    }

    // trig config
    gpio_config_t trigger_gpio_settings = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << trigger_gpio_pin),
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    err = gpio_config(&trigger_gpio_settings);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize hcsr04 sensor's trigger pin");
        return err;
    }

    // echo config
    gpio_config_t echo_gpio_settings = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << echo_gpio_pin),
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    err = gpio_config(&echo_gpio_settings);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize hcsr04 sensor's echo pin");
        return err;
    }

    // intialize state
    gpio_set_level(trigger_gpio_pin, 0);
    sensor->trigger_gpio_pin = trigger_gpio_pin;
    sensor->echo_gpio_pin = echo_gpio_pin;
    sensor->last_distance_cm = 0.0;
    sensor->echo_timeout_us = echo_timeout_us;

    ESP_LOGI(TAG, "successfully initialized hcsr04 sensor");
    return ESP_OK;
}

esp_err_t hcsr04_measure_distance(hcsr04_sensor_t *sensor)
{
    // sanity check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "hcsr04 sensor pointer is null");
        return ESP_ERR_INVALID_ARG;
    }

    // send trigger pulse
    gpio_set_level(sensor->trigger_gpio_pin, 0);
    esp_rom_delay_us(HCSR04_TRIGGER_SETTLE_LOW_US);
    gpio_set_level(sensor->trigger_gpio_pin, 1);
    esp_rom_delay_us(HCSR04_TRIGGER_PULSE_US);
    gpio_set_level(sensor->trigger_gpio_pin, 0);

    // wait for ECHO to go HIGH (with timeout)
    int64_t wait_start_us = esp_timer_get_time();
    while (gpio_get_level(sensor->echo_gpio_pin) == 0) {
        if ((esp_timer_get_time() - wait_start_us) > sensor->echo_timeout_us) {
            ESP_LOGW(TAG, "timeout waiting for ECHO HIGH");
            return ESP_ERR_TIMEOUT;
        }
    }

    // record when ECHO went HIGH
    int64_t echo_rise_us = esp_timer_get_time();

    // wait for ECHO to go LOW (with timeout)
    while (gpio_get_level(sensor->echo_gpio_pin) == 1) {
        if ((esp_timer_get_time() - echo_rise_us) > sensor->echo_timeout_us) {
            ESP_LOGW(TAG, "timeout waiting for ECHO LOW");
            return ESP_ERR_TIMEOUT;
        }
    }

    // record when ECHO went LOW
    int64_t echo_fall_us = esp_timer_get_time();

    // the echo pulse lasts as long as the sound's round trip
    int64_t echo_round_trip_us = echo_fall_us - echo_rise_us;

    sensor->last_distance_cm = (echo_round_trip_us * HCSR04_SPEED_OF_SOUND_CM_PER_US) / 2.0;
    ESP_LOGD(TAG, "distance: %.2f cm (pulse: %lld us)", sensor->last_distance_cm, echo_round_trip_us);

    ESP_LOGI(TAG, "successfully read distance of hcsr04 sensor: %0.02f", sensor->last_distance_cm);
    return ESP_OK;
}

float hcsr04_get_last_distance_cm(const hcsr04_sensor_t *sensor)
{
    // sanity check
    if (sensor == NULL) {
        ESP_LOGW(TAG, "hcsr04 pointer is null");
        return 0.0;
    }
    return sensor->last_distance_cm;
}
