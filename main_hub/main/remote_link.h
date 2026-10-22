/**
 * @file remote_link.h
 * @author Anthony Yalong
 * @brief The hub's BLE link to the remote node
 */
#ifndef REMOTE_LINK_H
#define REMOTE_LINK_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Called whenever the link's view of the remote changes.
 *
 * remote_motion_valid becomes true after receiving a valid motion payload and
 * false when the link becomes unusable. A BLE connection alone does not make
 * the reading valid. remote_motion_detected means something only while
 * remote_motion_valid is true.
 * It runs on NimBLE's host task, so it must not block.
 */
typedef void (*remote_link_state_callback_t)(bool remote_motion_valid, bool remote_motion_detected);

/**
 * @brief Start NimBLE and keep scanning for, connecting to, and subscribing to the remote node.
 *
 * A failure on the remote's side drops the connection and scans again; only
 * starting NimBLE itself can fail here.
 */
esp_err_t remote_link_start(remote_link_state_callback_t on_remote_state);

#endif // REMOTE_LINK_H
