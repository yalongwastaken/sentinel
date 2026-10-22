/**
 * @file crash_limit.h
 * @author Anthony Yalong
 * @brief Stops a firmware build that keeps crashing the same way
 *
 * A build that crashes CRASH_LIMIT_MAX_CRASHES_PER_TYPE times by one crash type
 * is treated as unreliable until a different build is flashed. Counts live in
 * NVS and belong to the running image: a different image SHA-256 clears them.
 */
#ifndef CRASH_LIMIT_H
#define CRASH_LIMIT_H

#include <stdbool.h>
#include "esp_err.h"

#define CRASH_LIMIT_MAX_CRASHES_PER_TYPE 5

typedef struct {
    bool limit_reached;
    const char *crash_type; // the type that reached the limit, or NULL
} crash_limit_verdict_t;

/**
 * @brief Count the last reset if it was a crash, then report whether any crash
 *        type has reached the limit for the running image.
 *
 * A crash is any reset the board did not ask for: a panic, a watchdog, a
 * brownout, a power glitch, a CPU lockup, or an eFuse error. Call it once per
 * boot, after nvs_flash_init(); what to do about the verdict is the caller's.
 */
esp_err_t crash_limit_record_last_reset(crash_limit_verdict_t *verdict);

#endif // CRASH_LIMIT_H
