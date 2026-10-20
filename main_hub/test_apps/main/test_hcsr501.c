/**
 * @file test_hcsr501.c
 * @author Anthony Yalong
 * @brief HC-SR501 on the hub's wiring
 */

#include <stdio.h>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "hcsr501.h"
#include "main_hub_system_config.h"

#define WAVE_WINDOW_US (10 * 1000 * 1000)
#define MOTION_POLL_MS 50

TEST_CASE("the PIR initializes on its wired pin", "[hcsr501]")
{
    hcsr501_sensor_t sensor;
    TEST_ASSERT_EQUAL(ESP_OK, hcsr501_init(&sensor, HCSR501_GPIO_PIN));
}

TEST_CASE("the PIR reports motion within 10 s of a wave", "[hcsr501][needs_person]")
{
    hcsr501_sensor_t sensor;
    TEST_ASSERT_EQUAL(ESP_OK, hcsr501_init(&sensor, HCSR501_GPIO_PIN));

    printf("wave a hand in front of the PIR within 10 s\n");
    int64_t deadline_us = esp_timer_get_time() + WAVE_WINDOW_US;
    bool motion_seen = false;
    while (!motion_seen && esp_timer_get_time() < deadline_us) {
        motion_seen = hcsr501_is_motion_detected(&sensor);
        vTaskDelay(pdMS_TO_TICKS(MOTION_POLL_MS));
    }
    TEST_ASSERT_TRUE_MESSAGE(motion_seen, "no motion seen; check the PIR's wiring and give it a minute to warm up");
}
