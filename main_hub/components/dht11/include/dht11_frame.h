/**
 * @file dht11_frame.h
 * @author Anthony Yalong
 * @brief Decoding of the DHT11's 40-bit frame, kept free of GPIO so it runs on a host
 */
#ifndef DHT11_FRAME_H
#define DHT11_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define DHT11_FRAME_BITS 40
#define DHT11_FRAME_BYTES 5

// an all-zero frame passes the checksum but is no real reading; callers decide what to do about it
#define DHT11_ERR_ALL_ZERO_FRAME ESP_ERR_INVALID_RESPONSE

typedef struct {
    float temperature_celsius;
    float relative_humidity_percent;
} dht11_reading_t;

/**
 * @brief Classify one bit by the width of its high pulse.
 *
 * The datasheet's pulses are about 26-28 us for '0' and about 70 us for '1'.
 */
bool dht11_is_one_bit(int64_t high_pulse_us);

/**
 * @brief Verify a frame's checksum and decode it.
 *
 * Bytes 0-1 are humidity (integer, decimal), bytes 2-3 temperature (integer,
 * decimal), byte 4 the checksum: the low 8 bits of the sum of bytes 0-3.
 *
 * @return ESP_OK with reading set; DHT11_ERR_ALL_ZERO_FRAME for an all-zero
 *         frame; ESP_ERR_INVALID_CRC for a checksum mismatch. reading is
 *         untouched on every failure.
 */
esp_err_t dht11_decode_frame(const uint8_t frame[DHT11_FRAME_BYTES], dht11_reading_t *reading);

#endif // DHT11_FRAME_H
