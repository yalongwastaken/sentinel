/**
 * @file hcsr04.c
 * @author Anthony Yalong
 * @brief HC-SR04 ultrasonic distance sensor driver implementation
 */

#include "hcsr04.h"
#include "hcsr04_echo.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "HCSR04";

#define HCSR04_TRIGGER_SETTLE_LOW_US 2
#define HCSR04_TRIGGER_PULSE_US 10

esp_err_t hcsr04_init(hcsr04_sensor_t *sensor, gpio_num_t trigger_gpio_pin, gpio_num_t echo_gpio_pin,
                      uint32_t echo_timeout_us)
{
    gpio_config_t trigger_gpio_settings = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << trigger_gpio_pin),
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    esp_err_t err = gpio_config(&trigger_gpio_settings);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure trigger gpio %d", trigger_gpio_pin);
        return err;
    }

    gpio_config_t echo_gpio_settings = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << echo_gpio_pin),
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    err = gpio_config(&echo_gpio_settings);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure echo gpio %d", echo_gpio_pin);
        return err;
    }

    gpio_set_level(trigger_gpio_pin, 0);
    sensor->trigger_gpio_pin = trigger_gpio_pin;
    sensor->echo_gpio_pin = echo_gpio_pin;
    sensor->echo_timeout_us = echo_timeout_us;

    ESP_LOGI(TAG, "initialized on trigger gpio %d, echo gpio %d", trigger_gpio_pin, echo_gpio_pin);
    return ESP_OK;
}

esp_err_t hcsr04_measure_distance_cm(const hcsr04_sensor_t *sensor, float *distance_cm)
{
    gpio_set_level(sensor->trigger_gpio_pin, 0);
    esp_rom_delay_us(HCSR04_TRIGGER_SETTLE_LOW_US);
    gpio_set_level(sensor->trigger_gpio_pin, 1);
    esp_rom_delay_us(HCSR04_TRIGGER_PULSE_US);
    gpio_set_level(sensor->trigger_gpio_pin, 0);

    int64_t wait_start_us = esp_timer_get_time();
    while (gpio_get_level(sensor->echo_gpio_pin) == 0) {
        if ((esp_timer_get_time() - wait_start_us) > sensor->echo_timeout_us) {
            ESP_LOGW(TAG, "timeout waiting for echo to rise");
            return ESP_ERR_TIMEOUT;
        }
    }
    int64_t echo_rise_us = esp_timer_get_time();

    while (gpio_get_level(sensor->echo_gpio_pin) == 1) {
        if ((esp_timer_get_time() - echo_rise_us) > sensor->echo_timeout_us) {
            ESP_LOGW(TAG, "timeout waiting for echo to fall");
            return ESP_ERR_TIMEOUT;
        }
    }
    int64_t echo_fall_us = esp_timer_get_time();

    int64_t echo_round_trip_us = echo_fall_us - echo_rise_us;
    *distance_cm = hcsr04_distance_cm_from_echo_us(echo_round_trip_us);
    ESP_LOGD(TAG, "distance_cm=%.2f echo_round_trip_us=%lld", *distance_cm, echo_round_trip_us);

    return ESP_OK;
}
