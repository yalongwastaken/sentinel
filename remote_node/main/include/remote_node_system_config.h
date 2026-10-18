/**
 * @file system_config.h
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

// pir configuration
#define PIR_GPIO_PIN GPIO_NUM_13
#define PIR_DEBOUNCE_TIME_MS 50

// task configuration
#define SENSOR_TASK_STACK_SIZE 4096
#define SENSOR_TASK_PRIORITY 5
#define SENSOR_READ_INTERVAL_MS 5000

#endif // REMOTE_NODE_SYSTEM_CONFIG_H