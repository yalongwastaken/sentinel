/**
 * @file test_hcsr04.c
 * @author Anthony Yalong
 * @brief HC-SR04 on the hub's wiring; needs something to echo off within 4 m
 */

#include "unity.h"
#include "esp_timer.h"
#include "hcsr04.h"
#include "main_hub_system_config.h"

// the datasheet's range
#define HCSR04_MIN_RANGE_CM 2.0f
#define HCSR04_MAX_RANGE_CM 400.0f
// one timed-out wait for the echo to rise, then one for it to fall
#define MEASUREMENT_BUDGET_US (2 * HCSR04_ECHO_TIMEOUT_US)

static hcsr04_sensor_t init_hcsr04(void)
{
    hcsr04_sensor_t sensor;
    TEST_ASSERT_EQUAL(ESP_OK,
                      hcsr04_init(&sensor, HCSR04_TRIGGER_GPIO_PIN, HCSR04_ECHO_GPIO_PIN, HCSR04_ECHO_TIMEOUT_US));
    return sensor;
}

TEST_CASE("a distance lands inside the sensor's 2-400 cm range", "[hcsr04]")
{
    hcsr04_sensor_t sensor = init_hcsr04();
    float distance_cm;
    TEST_ASSERT_EQUAL(ESP_OK, hcsr04_measure_distance_cm(&sensor, &distance_cm));
    TEST_ASSERT_TRUE_MESSAGE(distance_cm >= HCSR04_MIN_RANGE_CM && distance_cm <= HCSR04_MAX_RANGE_CM,
                             "distance outside 2-400 cm");
}

TEST_CASE("a measurement finishes inside its echo timeout budget", "[hcsr04]")
{
    hcsr04_sensor_t sensor = init_hcsr04();
    float distance_cm;
    int64_t start_us = esp_timer_get_time();
    hcsr04_measure_distance_cm(&sensor, &distance_cm);
    TEST_ASSERT_LESS_THAN_INT32(MEASUREMENT_BUDGET_US, (int32_t)(esp_timer_get_time() - start_us));
}
