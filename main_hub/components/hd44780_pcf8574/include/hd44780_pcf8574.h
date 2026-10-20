/**
 * @file hd44780_pcf8574.h
 * @author Anthony Yalong
 * @brief HD44780 character LCD driver over a PCF8574 I2C backpack
 */
#ifndef HD44780_PCF8574_H
#define HD44780_PCF8574_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct {
    i2c_master_dev_handle_t device;
    uint8_t columns;
    uint8_t rows;
} hd44780_t;

/**
 * @brief Add the backpack to the I2C bus as a device, then run the HD44780
 *        4-bit initialization sequence with the backlight on.
 *
 * @param i2c_address the backpack's address, typically 0x27 or 0x3F
 */
esp_err_t hd44780_init(hd44780_t *lcd, i2c_master_bus_handle_t bus, uint8_t i2c_address, uint32_t i2c_clock_hz,
                       uint8_t columns, uint8_t rows);

esp_err_t hd44780_clear(hd44780_t *lcd);

/**
 * @brief Move the cursor, counting columns and rows from 0.
 *
 * @return ESP_ERR_INVALID_ARG if the position is off the display
 */
esp_err_t hd44780_set_cursor(hd44780_t *lcd, uint8_t column, uint8_t row);

esp_err_t hd44780_print(hd44780_t *lcd, const char *text);

/**
 * @brief Format like printf and print; output longer than the driver's buffer is cut off.
 */
esp_err_t hd44780_printf(hd44780_t *lcd, const char *format, ...);

#endif // HD44780_PCF8574_H
