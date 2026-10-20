/**
 * @file test_hcsr04_echo.c
 * @author Anthony Yalong
 * @brief Echo time to distance, checked against the physics rather than the code's own constant
 */

#include "unity.h"
#include "hcsr04_echo.h"

// sound travels 343 m/s in dry air at 20 C
#define REFERENCE_SPEED_OF_SOUND_CM_PER_US (343.0 * 100.0 / 1000000.0)
#define HCSR04_MAX_RANGE_CM 400.0

static double reference_distance_cm_from_echo_us(int64_t echo_round_trip_us)
{
    return echo_round_trip_us * REFERENCE_SPEED_OF_SOUND_CM_PER_US / 2.0;
}

TEST_CASE("a zero-length echo is zero distance", "[hcsr04]")
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, hcsr04_distance_cm_from_echo_us(0));
}

TEST_CASE("distance matches the speed of sound across the range", "[hcsr04]")
{
    const int64_t echo_round_trips_us[] = {100, 1000, 5000, 11662, 20000};
    for (int i = 0; i < sizeof(echo_round_trips_us) / sizeof(echo_round_trips_us[0]); i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.05f, reference_distance_cm_from_echo_us(echo_round_trips_us[i]),
                                 hcsr04_distance_cm_from_echo_us(echo_round_trips_us[i]));
    }
}

TEST_CASE("the echo for the sensor's maximum range reads as that range", "[hcsr04]")
{
    int64_t max_range_echo_us = (int64_t)(HCSR04_MAX_RANGE_CM * 2.0 / REFERENCE_SPEED_OF_SOUND_CM_PER_US);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, HCSR04_MAX_RANGE_CM, hcsr04_distance_cm_from_echo_us(max_range_echo_us));
}
