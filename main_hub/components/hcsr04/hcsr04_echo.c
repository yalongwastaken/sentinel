/**
 * @file hcsr04_echo.c
 * @author Anthony Yalong
 * @brief HC-SR04 echo time to distance
 */

#include "hcsr04_echo.h"

#define HCSR04_SPEED_OF_SOUND_CM_PER_US 0.0343 // 343 m/s, dry air at 20 C

float hcsr04_distance_cm_from_echo_us(int64_t echo_round_trip_us)
{
    return (echo_round_trip_us * HCSR04_SPEED_OF_SOUND_CM_PER_US) / 2.0;
}
