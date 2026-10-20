/**
 * @file hcsr501.c
 * @author Anthony Yalong
 * @brief HC-SR501 PIR motion sensor driver implementation
 */

#include "hcsr501.h"
#include "esp_log.h"

static const char *TAG = "HCSR501";

esp_err_t hcsr501_init(hcsr501_sensor_t *sensor, gpio_num_t gpio_pin)
{
    gpio_config_t gpio_settings = {.pin_bit_mask = (1ULL << gpio_pin),
                                   .mode = GPIO_MODE_INPUT,
                                   .intr_type = GPIO_INTR_DISABLE,
                                   .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                   .pull_up_en = GPIO_PULLUP_DISABLE};
    esp_err_t err = gpio_config(&gpio_settings);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure gpio %d", gpio_pin);
        return err;
    }

    sensor->gpio_pin = gpio_pin;
    ESP_LOGI(TAG, "initialized on gpio %d", gpio_pin);
    return ESP_OK;
}

bool hcsr501_is_motion_detected(const hcsr501_sensor_t *sensor) { return gpio_get_level(sensor->gpio_pin); }
