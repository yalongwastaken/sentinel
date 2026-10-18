/**
 * @file hd44780_pcf8574.h
 * @author Anthony Yalong
 * @brief I2C LCD1602 display driver
 */
#ifndef HD44780_PCF8574_H
#define HD44780_PCF8574_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// hd44780 commands
#define HD44780_CLEAR_DISPLAY 0x01
#define LCD_CMD_HOME 0x02
#define HD44780_ENTRY_MODE_SET 0x04
#define HD44780_DISPLAY_CONTROL 0x08
#define HD44780_FUNCTION_SET 0x20
#define HD44780_SET_DDRAM_ADDRESS 0x80

// hd44780 command flags
#define HD44780_ENTRY_INCREMENT 0x02
#define HD44780_DISPLAY_ON 0x04
#define HD44780_4_BIT_BUS 0x00
#define HD44780_2_LINES 0x08
#define HD44780_5X8_FONT 0x00

// pcf8574 i/o expander pins
#define PCF8574_BACKLIGHT_ON_BIT 0x08
#define PCF8574_BACKLIGHT_OFF 0x00
#define PCF8574_ENABLE_BIT 0x04
#define LCD_RW 0x02 // read/write bit
#define PCF8574_REGISTER_SELECT_BIT 0x01

// i2c timeout
#define PCF8574_I2C_TIMEOUT_MS 1000

/**
 * @brief LCD handle structure
 */
typedef struct {
    i2c_master_dev_handle_t device;
    uint8_t columns;
    uint8_t rows;
    uint8_t backlight_bit;
} hd44780_t;

/**
 * @brief Initialize LCD display
 *
 * Adds the LCD to the given I2C master bus as a device, then runs the
 * HD44780 4-bit initialization sequence.
 *
 * @param lcd Pointer to LCD handle
 * @param bus I2C master bus the LCD's PCF8574 backpack is wired to
 * @param i2c_address I2C address of the backpack (typically 0x27 or 0x3F)
 * @param i2c_clock_hz I2C clock for this device
 * @param columns Number of columns (typically 16)
 * @param rows Number of rows (typically 2)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t hd44780_init(hd44780_t *lcd, i2c_master_bus_handle_t bus, uint8_t i2c_address, uint32_t i2c_clock_hz,
                       uint8_t columns, uint8_t rows);

/**
 * @brief Clear LCD display
 *
 * @param lcd Pointer to LCD handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t hd44780_clear(hd44780_t *lcd);

/**
 * @brief Set cursor position
 *
 * @param lcd Pointer to LCD handle
 * @param column Column, from 0
 * @param row Row, from 0
 * @return esp_err_t ESP_OK on success
 */
esp_err_t hd44780_set_cursor(hd44780_t *lcd, uint8_t column, uint8_t row);

/**
 * @brief Turn the LCD backlight on or off
 *
 * @param lcd Pointer to LCD handle
 * @param on true = on, false = off
 * @return esp_err_t ESP_OK on success
 */
esp_err_t hd44780_set_backlight(hd44780_t *lcd, bool on);

/**
 * @brief Print string to LCD
 *
 * @param lcd Pointer to LCD handle
 * @param text String to print
 * @return esp_err_t ESP_OK on success
 */
esp_err_t hd44780_print(hd44780_t *lcd, const char *text);

/**
 * @brief Print formatted string to LCD (like printf)
 *
 * @param lcd Pointer to LCD handle
 * @param format Format string
 * @param ... Variable arguments
 * @return esp_err_t ESP_OK on success
 */
esp_err_t hd44780_printf(hd44780_t *lcd, const char *format, ...);

#endif // HD44780_PCF8574_H
