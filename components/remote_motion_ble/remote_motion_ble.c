/**
 * @file remote_motion_ble.c
 * @author Anthony Yalong
 * @brief Encoding of the remote node's motion payload
 */

#include "remote_motion_ble.h"

#define MOTION_PAYLOAD_NO_MOTION 0
#define MOTION_PAYLOAD_MOTION 1

uint8_t remote_motion_ble_encode_motion(bool motion_detected)
{
    return motion_detected ? MOTION_PAYLOAD_MOTION : MOTION_PAYLOAD_NO_MOTION;
}

esp_err_t remote_motion_ble_decode_motion(uint8_t payload, bool *motion_detected)
{
    if (payload != MOTION_PAYLOAD_NO_MOTION && payload != MOTION_PAYLOAD_MOTION) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *motion_detected = payload == MOTION_PAYLOAD_MOTION;
    return ESP_OK;
}
