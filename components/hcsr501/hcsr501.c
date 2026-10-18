/**
 * @file hcsr501.c
 * @author Anthony Yalong
 * @brief PIR motion sensor driver implementation
 */

#include "hcsr501.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static const char *TAG = "HCSR501";

esp_err_t hcsr501_init(hcsr501_sensor_t *sensor, gpio_num_t gpio_pin, uint32_t debounce_ms)
{
    // logging
    esp_err_t err;

    // parameter check
    if (sensor == NULL) {
        ESP_LOGE(TAG, "sensor pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // configure gpio
    gpio_config_t gpio_settings = {.pin_bit_mask = (1ULL << gpio_pin),
                                   .mode = GPIO_MODE_INPUT,
                                   .intr_type = GPIO_INTR_DISABLE,
                                   .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                   .pull_up_en = GPIO_PULLUP_DISABLE};
    err = gpio_config(&gpio_settings);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to config sensor gpio");
        return err;
    }

    // initialize sensor structure
    sensor->gpio_pin = gpio_pin;
    sensor->previous_level = false;
    sensor->motion_event_count = 0;
    sensor->debounce_ms = debounce_ms;
    sensor->last_motion_event_us = 0;

    ESP_LOGI(TAG, "sensor initialized");
    return ESP_OK;
}

bool hcsr501_read_level_and_count_motion_events(hcsr501_sensor_t *sensor)
{
    if (sensor == NULL) {
        ESP_LOGE(TAG, "sensor pointer is NULL");
        return false;
    }

    // read current level
    bool current_level = gpio_get_level(sensor->gpio_pin);
    int64_t now_us = esp_timer_get_time();

    // detect rising edge (LOW -> HIGH)
    if (current_level && !sensor->previous_level) {
        // debounce timing
        int64_t ms_since_last_motion_event = (now_us - sensor->last_motion_event_us) / 1000;

        if (ms_since_last_motion_event >= sensor->debounce_ms) {
            sensor->motion_event_count++;
            sensor->last_motion_event_us = now_us;
            ESP_LOGD(TAG, "motion detected! count: %lu", sensor->motion_event_count);
        }
    }

    // update previous level
    sensor->previous_level = current_level;

    return current_level;
}

uint32_t hcsr501_get_motion_event_count(const hcsr501_sensor_t *sensor)
{
    if (sensor == NULL) {
        ESP_LOGE(TAG, "sensor pointer is NULL");
        return 0;
    }
    return sensor->motion_event_count;
}

void hcsr501_reset_motion_event_count(hcsr501_sensor_t *sensor)
{
    if (sensor != NULL) {
        sensor->motion_event_count = 0;
        ESP_LOGI(TAG, "Motion count reset");
    }
}
