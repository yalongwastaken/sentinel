/**
 * @file remote_motion_ble.h
 * @author Anthony Yalong
 * @brief The BLE contract between the remote node, which serves motion, and the hub, which subscribes to it
 *
 * Both firmware images build from this one definition, so their names, UUIDs, and payload cannot drift apart.
 */
#ifndef REMOTE_MOTION_BLE_H
#define REMOTE_MOTION_BLE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define REMOTE_MOTION_BLE_DEVICE_NAME "sentinel-remote"

// 0fe18715-b7e8-4b86-a8e1-2e9dac8a06d2, least significant byte first as BLE_UUID128_INIT expects
#define REMOTE_MOTION_BLE_SERVICE_UUID_BYTES                                                                           \
    0xd2, 0x06, 0x8a, 0xac, 0x9d, 0x2e, 0xe1, 0xa8, 0x86, 0x4b, 0xe8, 0xb7, 0x15, 0x87, 0xe1, 0x0f

// 830c6170-11e8-4739-970a-b5906cbf6960, least significant byte first as BLE_UUID128_INIT expects
#define REMOTE_MOTION_BLE_MOTION_CHARACTERISTIC_UUID_BYTES                                                             \
    0x60, 0x69, 0xbf, 0x6c, 0x90, 0xb5, 0x0a, 0x97, 0x39, 0x47, 0xe8, 0x11, 0x70, 0x61, 0x0c, 0x83

#define REMOTE_MOTION_BLE_MOTION_PAYLOAD_BYTES 1

/**
 * @brief Encode motion as the characteristic's one-byte payload: 1 for motion, 0 for none.
 */
uint8_t remote_motion_ble_encode_motion(bool motion_detected);

/**
 * @brief Decode the characteristic's one-byte payload.
 *
 * @return ESP_OK with motion_detected set, or ESP_ERR_INVALID_RESPONSE for any
 *         byte other than 0 or 1, leaving motion_detected untouched
 */
esp_err_t remote_motion_ble_decode_motion(uint8_t payload, bool *motion_detected);

#endif // REMOTE_MOTION_BLE_H
