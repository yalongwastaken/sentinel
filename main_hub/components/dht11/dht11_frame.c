/**
 * @file dht11_frame.c
 * @author Anthony Yalong
 * @brief Decoding of the DHT11's 40-bit frame
 */

#include "dht11_frame.h"

#define DHT11_ONE_BIT_MIN_HIGH_US 40
#define DHT11_HUMIDITY_INTEGER_BYTE 0
#define DHT11_TEMPERATURE_INTEGER_BYTE 2
#define DHT11_CHECKSUM_BYTE 4

bool dht11_is_one_bit(int64_t high_pulse_us) { return high_pulse_us > DHT11_ONE_BIT_MIN_HIGH_US; }

esp_err_t dht11_decode_frame(const uint8_t frame[DHT11_FRAME_BYTES], dht11_reading_t *reading)
{
    uint8_t computed_checksum = frame[0] + frame[1] + frame[2] + frame[3];
    if (computed_checksum != frame[DHT11_CHECKSUM_BYTE]) {
        return ESP_ERR_INVALID_CRC;
    }

    reading->relative_humidity_percent = frame[DHT11_HUMIDITY_INTEGER_BYTE];
    reading->temperature_celsius = frame[DHT11_TEMPERATURE_INTEGER_BYTE];
    return ESP_OK;
}
