/**
 * @file test_dht11_frame.c
 * @author Anthony Yalong
 * @brief DHT11 bit classification and frame decoding
 */

#include "unity.h"
#include "dht11_frame.h"

#define UNTOUCHED_SENTINEL -1.0f

TEST_CASE("datasheet pulse widths classify as 0 and 1", "[dht11]")
{
    TEST_ASSERT_FALSE(dht11_is_one_bit(27));
    TEST_ASSERT_TRUE(dht11_is_one_bit(70));
}

TEST_CASE("the 0/1 threshold sits between 40 and 41 us", "[dht11]")
{
    TEST_ASSERT_FALSE(dht11_is_one_bit(40));
    TEST_ASSERT_TRUE(dht11_is_one_bit(41));
}

TEST_CASE("a valid frame decodes to humidity and temperature", "[dht11]")
{
    const uint8_t frame[DHT11_FRAME_BYTES] = {55, 0, 23, 0, 78};
    dht11_reading_t reading;
    TEST_ASSERT_EQUAL(ESP_OK, dht11_decode_frame(frame, &reading));
    TEST_ASSERT_EQUAL_FLOAT(55.0f, reading.relative_humidity_percent);
    TEST_ASSERT_EQUAL_FLOAT(23.0f, reading.temperature_celsius);
}

TEST_CASE("a checksum that wraps past 255 still verifies", "[dht11]")
{
    const uint8_t frame[DHT11_FRAME_BYTES] = {90, 0, 200, 0, (uint8_t)(90 + 200)};
    dht11_reading_t reading;
    TEST_ASSERT_EQUAL(ESP_OK, dht11_decode_frame(frame, &reading));
}

TEST_CASE("a bad checksum is rejected and leaves the reading untouched", "[dht11]")
{
    const uint8_t frame[DHT11_FRAME_BYTES] = {55, 0, 23, 0, 79};
    dht11_reading_t reading = {.temperature_celsius = UNTOUCHED_SENTINEL,
                               .relative_humidity_percent = UNTOUCHED_SENTINEL};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, dht11_decode_frame(frame, &reading));
    TEST_ASSERT_EQUAL_FLOAT(UNTOUCHED_SENTINEL, reading.temperature_celsius);
    TEST_ASSERT_EQUAL_FLOAT(UNTOUCHED_SENTINEL, reading.relative_humidity_percent);
}

TEST_CASE("an all-zero frame is rejected even though its checksum matches", "[dht11]")
{
    const uint8_t frame[DHT11_FRAME_BYTES] = {0, 0, 0, 0, 0};
    dht11_reading_t reading = {.temperature_celsius = UNTOUCHED_SENTINEL,
                               .relative_humidity_percent = UNTOUCHED_SENTINEL};
    TEST_ASSERT_EQUAL(DHT11_ERR_ALL_ZERO_FRAME, dht11_decode_frame(frame, &reading));
    TEST_ASSERT_EQUAL_FLOAT(UNTOUCHED_SENTINEL, reading.relative_humidity_percent);
}
