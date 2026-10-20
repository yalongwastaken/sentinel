/**
 * @file hd44780_pcf8574.c
 * @author Anthony Yalong
 * @brief HD44780 character LCD driver over a PCF8574 I2C backpack
 */

#include <stdarg.h>
#include <stdio.h>
#include "hd44780_pcf8574.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HD44780";

#define HD44780_CLEAR_DISPLAY 0x01
#define HD44780_ENTRY_MODE_SET 0x04
#define HD44780_DISPLAY_CONTROL 0x08
#define HD44780_FUNCTION_SET 0x20
#define HD44780_SET_DDRAM_ADDRESS 0x80

#define HD44780_ENTRY_INCREMENT 0x02
#define HD44780_DISPLAY_ON 0x04
#define HD44780_4_BIT_BUS 0x00
#define HD44780_2_LINES 0x08
#define HD44780_5X8_FONT 0x00

#define HD44780_WAKE_UP_NIBBLE 0x30
#define HD44780_SELECT_4_BIT_NIBBLE 0x20
#define HD44780_POWER_ON_WAIT_MS 50
#define HD44780_WAKE_UP_WAIT_MS 5
#define HD44780_COMMAND_WAIT_MS 1
#define HD44780_CLEAR_DISPLAY_WAIT_MS 2
#define HD44780_INIT_SETTLE_WAIT_MS 10
#define HD44780_PRINTF_BUFFER_BYTES 64

#define PCF8574_REGISTER_SELECT_BIT 0x01
#define PCF8574_ENABLE_BIT 0x04
#define PCF8574_BACKLIGHT_ON_BIT 0x08
#define PCF8574_ENABLE_PULSE_WAIT_MS 1
#define PCF8574_I2C_TIMEOUT_MS 1000

#define HIGH_NIBBLE_MASK 0xF0

static esp_err_t write_with_enable_pulse(hd44780_t *lcd, uint8_t backpack_bits)
{
    uint8_t byte = backpack_bits | PCF8574_ENABLE_BIT;
    esp_err_t enable_high_err = i2c_master_transmit(lcd->device, &byte, sizeof(byte), PCF8574_I2C_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(PCF8574_ENABLE_PULSE_WAIT_MS));

    byte = backpack_bits & ~PCF8574_ENABLE_BIT;
    esp_err_t enable_low_err = i2c_master_transmit(lcd->device, &byte, sizeof(byte), PCF8574_I2C_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(PCF8574_ENABLE_PULSE_WAIT_MS));

    return enable_high_err != ESP_OK ? enable_high_err : enable_low_err;
}

// every byte to the backpack carries the backlight bit, or the backlight goes dark
static esp_err_t write_nibble(hd44780_t *lcd, uint8_t nibble, uint8_t register_select)
{
    return write_with_enable_pulse(lcd, nibble | register_select | PCF8574_BACKLIGHT_ON_BIT);
}

static esp_err_t write_byte(hd44780_t *lcd, uint8_t byte, uint8_t register_select)
{
    esp_err_t err = write_nibble(lcd, byte & HIGH_NIBBLE_MASK, register_select);
    if (err != ESP_OK) {
        return err;
    }
    return write_nibble(lcd, (byte << 4) & HIGH_NIBBLE_MASK, register_select);
}

static esp_err_t send_command(hd44780_t *lcd, uint8_t command) { return write_byte(lcd, command, 0); }

static esp_err_t send_character(hd44780_t *lcd, uint8_t character)
{
    return write_byte(lcd, character, PCF8574_REGISTER_SELECT_BIT);
}

esp_err_t hd44780_init(hd44780_t *lcd, i2c_master_bus_handle_t bus, uint8_t i2c_address, uint32_t i2c_clock_hz,
                       uint8_t columns, uint8_t rows)
{
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_address,
        .scl_speed_hz = i2c_clock_hz,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &device_config, &lcd->device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to add 0x%02X to the i2c bus", i2c_address);
        return err;
    }

    lcd->columns = columns;
    lcd->rows = rows;

    // the HD44780 power-on reset sequence: three wake-ups, then switch to the 4-bit bus
    vTaskDelay(pdMS_TO_TICKS(HD44780_POWER_ON_WAIT_MS));
    write_nibble(lcd, HD44780_WAKE_UP_NIBBLE, 0);
    vTaskDelay(pdMS_TO_TICKS(HD44780_WAKE_UP_WAIT_MS));
    write_nibble(lcd, HD44780_WAKE_UP_NIBBLE, 0);
    vTaskDelay(pdMS_TO_TICKS(HD44780_WAKE_UP_WAIT_MS));
    write_nibble(lcd, HD44780_WAKE_UP_NIBBLE, 0);
    vTaskDelay(pdMS_TO_TICKS(HD44780_COMMAND_WAIT_MS));
    write_nibble(lcd, HD44780_SELECT_4_BIT_NIBBLE, 0);
    vTaskDelay(pdMS_TO_TICKS(HD44780_COMMAND_WAIT_MS));

    send_command(lcd, HD44780_FUNCTION_SET | HD44780_4_BIT_BUS | HD44780_2_LINES | HD44780_5X8_FONT);
    send_command(lcd, HD44780_DISPLAY_CONTROL | HD44780_DISPLAY_ON);
    hd44780_clear(lcd);
    send_command(lcd, HD44780_ENTRY_MODE_SET | HD44780_ENTRY_INCREMENT);
    vTaskDelay(pdMS_TO_TICKS(HD44780_INIT_SETTLE_WAIT_MS));

    ESP_LOGI(TAG, "initialized at 0x%02X (%dx%d)", i2c_address, columns, rows);
    return ESP_OK;
}

esp_err_t hd44780_clear(hd44780_t *lcd)
{
    esp_err_t err = send_command(lcd, HD44780_CLEAR_DISPLAY);
    vTaskDelay(pdMS_TO_TICKS(HD44780_CLEAR_DISPLAY_WAIT_MS));
    return err;
}

esp_err_t hd44780_set_cursor(hd44780_t *lcd, uint8_t column, uint8_t row)
{
    if (column >= lcd->columns || row >= lcd->rows) {
        ESP_LOGE(TAG, "cursor position off the display: column %d, row %d", column, row);
        return ESP_ERR_INVALID_ARG;
    }

    // HD44780 DDRAM is not contiguous across rows; these are where rows 0-3 start
    static const uint8_t ddram_row_start_addresses[] = {0x00, 0x40, 0x14, 0x54};
    uint8_t ddram_address = column + ddram_row_start_addresses[row];
    return send_command(lcd, HD44780_SET_DDRAM_ADDRESS | ddram_address);
}

esp_err_t hd44780_print(hd44780_t *lcd, const char *text)
{
    while (*text) {
        esp_err_t err = send_character(lcd, *text++);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t hd44780_printf(hd44780_t *lcd, const char *format, ...)
{
    char formatted_text[HD44780_PRINTF_BUFFER_BYTES];
    va_list args;
    va_start(args, format);
    vsnprintf(formatted_text, sizeof(formatted_text), format, args);
    va_end(args);
    return hd44780_print(lcd, formatted_text);
}
