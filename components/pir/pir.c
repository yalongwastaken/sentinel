/**
 * @file pir.c
 * @author Anthony Yalong
 * @brief PIR motion sensor driver implementation
 */

#include "pir.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static const char *TAG = "PIR";

esp_err_t pir_init(pir_sensor_t *pir, gpio_num_t pin, uint32_t debounce_time)
{
    // logging
    esp_err_t ret;

    // parameter check
    if (pir == NULL) {
        ESP_LOGE(TAG, "pir pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // configure gpio
    gpio_config_t pir_config = {.pin_bit_mask = (1ULL << pin),
                                .mode = GPIO_MODE_INPUT,
                                .intr_type = GPIO_INTR_DISABLE,
                                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                .pull_up_en = GPIO_PULLUP_DISABLE};
    ret = gpio_config(&pir_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to config pir gpio");
        return ret;
    }

    // initialize pir structure
    pir->pin_num = pin;
    pir->last_state = false;
    pir->motion_count = 0;
    pir->debounce_ms = debounce_time;
    pir->last_trigger_time = 0;

    ESP_LOGI(TAG, "pir initialized");
    return ESP_OK;
}

bool pir_read(pir_sensor_t *pir)
{
    if (pir == NULL) {
        ESP_LOGE(TAG, "pir pointer is NULL");
        return false;
    }

    // read current state
    bool current_state = gpio_get_level(pir->pin_num);
    int64_t current_time = esp_timer_get_time();

    // detect rising edge (LOW -> HIGH)
    if (current_state && !pir->last_state) {
        // debounce timing
        int64_t time_diff_ms = (current_time - pir->last_trigger_time) / 1000;

        if (time_diff_ms >= pir->debounce_ms) {
            pir->motion_count++;
            pir->last_trigger_time = current_time;
            ESP_LOGD(TAG, "motion detected! count: %lu", pir->motion_count);
        }
    }

    // update last state
    pir->last_state = current_state;

    return current_state;
}

uint32_t pir_get_motion_count(const pir_sensor_t *pir)
{
    if (pir == NULL) {
        ESP_LOGE(TAG, "pir pointer is NULL");
        return 0;
    }
    return pir->motion_count;
}

void pir_reset_motion_count(pir_sensor_t *pir)
{
    if (pir != NULL) {
        pir->motion_count = 0;
        ESP_LOGI(TAG, "Motion count reset");
    }
}