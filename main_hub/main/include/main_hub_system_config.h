/**
 * @file main_hub_system_config.h
 * @author Anthony Yalong
 * @brief System-wide configuration header defining GPIO mappings and
 *        timing constants for connected sensors.
 */

#ifndef MAIN_HUB_SYSTEM_CONFIG_H
#define MAIN_HUB_SYSTEM_CONFIG_H

// imports
#include <driver/gpio.h>
#include <esp_err.h>

// hcsr501 configuration
#define HCSR501_GPIO_PIN GPIO_NUM_13
#define HCSR501_DEBOUNCE_MS 50

// hcsr04 configuration
#define HCSR04_TRIGGER_GPIO_PIN GPIO_NUM_12
#define HCSR04_ECHO_GPIO_PIN GPIO_NUM_14
#define HCSR04_ECHO_TIMEOUT_US 30000
#define HCSR04_DISTANCE_CHANGE_THRESHOLD_CM 1.0

// dht11 configuration
#define DHT11_GPIO_PIN GPIO_NUM_27

// i2c Configuration
#define I2C_SCL_GPIO_PIN GPIO_NUM_22
#define I2C_SDA_GPIO_PIN GPIO_NUM_21
#define I2C_PORT I2C_NUM_0
#define PCF8574_I2C_CLOCK_HZ 100000
#define I2C_MASTER_TIMEOUT_MS 1000
#define I2C_GLITCH_FILTER_CYCLES 7 // espressif's recommended default

// lcd1602 configuration
#define PCF8574_I2C_ADDRESS 0x27
#define LCD1602_COLUMNS 16
#define LCD1602_ROWS 2

// task configuration
#define TASK_STACK_BYTES 4096
#define LOCAL_MOTION_TASK_PRIORITY 5
#define LOCAL_MOTION_TASK_PERIOD_MS 100
#define DISTANCE_TASK_PRIORITY 4
#define DISTANCE_TASK_PERIOD_MS 200
#define TEMPERATURE_HUMIDITY_TASK_PRIORITY 3
#define TEMPERATURE_HUMIDITY_TASK_PERIOD_MS 3000
#define BLE_CLIENT_TASK_PRIORITY 3
#define DISPLAY_REFRESH_TASK_PRIORITY 2
#define DISPLAY_REFRESH_TASK_PERIOD_MS 1000
#define STARTUP_MESSAGE_MS 2000

#endif // MAIN_HUB_SYSTEM_CONFIG_H
