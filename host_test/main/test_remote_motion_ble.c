/**
 * @file test_remote_motion_ble.c
 * @author Anthony Yalong
 * @brief The motion payload the remote encodes is the one the hub decodes
 */

#include "unity.h"
#include "remote_motion_ble.h"

TEST_CASE("motion and no motion survive an encode and decode round trip", "[remote_motion_ble]")
{
    const bool motion_states[] = {false, true};
    for (int i = 0; i < 2; i++) {
        bool decoded_motion = !motion_states[i];
        TEST_ASSERT_EQUAL(ESP_OK, remote_motion_ble_decode_motion(remote_motion_ble_encode_motion(motion_states[i]),
                                                                  &decoded_motion));
        TEST_ASSERT_EQUAL(motion_states[i], decoded_motion);
    }
}

TEST_CASE("a payload byte other than 0 or 1 is rejected and leaves the output untouched", "[remote_motion_ble]")
{
    const uint8_t invalid_payloads[] = {2, 0x7f, 0xff};
    for (int i = 0; i < 3; i++) {
        bool decoded_motion = true;
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE,
                          remote_motion_ble_decode_motion(invalid_payloads[i], &decoded_motion));
        TEST_ASSERT_TRUE(decoded_motion);
    }
}
