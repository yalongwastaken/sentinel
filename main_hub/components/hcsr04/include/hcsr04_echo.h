/**
 * @file hcsr04_echo.h
 * @author Anthony Yalong
 * @brief HC-SR04 echo time to distance, kept free of GPIO so it runs on a host
 */
#ifndef HCSR04_ECHO_H
#define HCSR04_ECHO_H

#include <stdint.h>

/**
 * @brief Convert the echo pulse width, which lasts the sound's round trip, to
 *        the one-way distance to the obstacle.
 */
float hcsr04_distance_cm_from_echo_us(int64_t echo_round_trip_us);

#endif // HCSR04_ECHO_H
