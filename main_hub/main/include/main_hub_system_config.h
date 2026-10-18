/**
 * @file system_config.h
 * @author Anthony Yalong
 * @brief System-wide configuration header defining GPIO mappings and
 *        timing constants for connected sensors.
 */

#ifndef MAIN_HUB_SYSTEM_CONFIG_H
#define MAIN_HUB_SYSTEM_CONFIG_H

// imports
#include <driver/gpio.h>
#include <esp_err.h>

// pir configuration
#define PIR_GPIO_PIN GPIO_NUM_13
#define PIR_DEBOUNCE_TIME_MS 50

// hcsr04 configuration
#define HCSR04_PIN_TRIG GPIO_NUM_12
#define HCSR04_PIN_ECHO GPIO_NUM_14
#define HCSR04_TIMEOUT_US 30000
#define HCSR04_DISTANCE_CHANGE_THRESHOLD_CM 1.0

// dht11 configuration
#define DHT11_GPIO_PIN GPIO_NUM_27

// i2c Configuration
#define I2C_MASTER_SCL_IO GPIO_NUM_22
#define I2C_MASTER_SDA_IO GPIO_NUM_21
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_TIMEOUT_MS 1000
#define I2C_MASTER_GLITCH_FILTER_CYCLES 7 // espressif's recommended default

// lcd Configuration
#define LCD_ADDR 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2

#endif // MAIN_HUB_SYSTEM_CONFIG_H
