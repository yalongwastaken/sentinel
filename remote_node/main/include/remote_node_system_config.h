/**
 * @file remote_node_system_config.h
 * @author Anthony Yalong
 * @brief Remote node system-wide configuration header defining GPIO mappings and
 *        timing constants for connected sensors.
 */

#ifndef REMOTE_NODE_SYSTEM_CONFIG_H
#define REMOTE_NODE_SYSTEM_CONFIG_H

// imports
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>

// ble configuration
#define BLE_DEVICE_NAME "ESP32_REMOTE"
#define BLE_SERVICE_UUID 0x180A
#define BLE_MOTION_CHAR_UUID 0x2A58

// hcsr501 configuration
#define HCSR501_GPIO_PIN GPIO_NUM_13
#define HCSR501_DEBOUNCE_MS 50

// task configuration
#define MOTION_SAMPLE_TASK_STACK_BYTES 4096
#define MOTION_SAMPLE_TASK_PRIORITY 5
#define MOTION_SAMPLE_TASK_PERIOD_MS 5000

#endif // REMOTE_NODE_SYSTEM_CONFIG_H
