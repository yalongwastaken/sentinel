/**
 * @file hd44780_pcf8574.c
 * @author Anthony Yalong
 * @brief I2C LCD1602 display driver implementation
 */

#include "hd44780_pcf8574.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdarg.h>
#include <stdio.h>

static const char *TAG = "HD44780";

#define HD44780_WAKE_UP_NIBBLE 0x30
#define HD44780_SELECT_4_BIT_NIBBLE 0x20
#define HD44780_POWER_ON_WAIT_MS 50
#define HD44780_WAKE_UP_WAIT_MS 5
#define HD44780_COMMAND_WAIT_MS 1
#define HD44780_CLEAR_DISPLAY_WAIT_MS 2
#define HD44780_INIT_SETTLE_WAIT_MS 10
#define PCF8574_ENABLE_PULSE_WAIT_MS 1
#define HIGH_NIBBLE_MASK 0xF0
#define HD44780_PRINTF_BUFFER_BYTES 64

// ============================================================================
// Helper Functions
// ============================================================================

static esp_err_t lcd_write_with_enable_pulse(hd44780_t *lcd, uint8_t backpack_bits)
{
    esp_err_t err;
    uint8_t byte;

    // pulse enable high
    byte = backpack_bits | PCF8574_ENABLE_BIT;
    err = i2c_master_transmit(lcd->device, &byte, sizeof(byte), PCF8574_I2C_TIMEOUT_MS);

    vTaskDelay(pdMS_TO_TICKS(PCF8574_ENABLE_PULSE_WAIT_MS));

    // pulse enable low
    byte = backpack_bits & ~PCF8574_ENABLE_BIT;
    err = i2c_master_transmit(lcd->device, &byte, sizeof(byte), PCF8574_I2C_TIMEOUT_MS);

    vTaskDelay(pdMS_TO_TICKS(PCF8574_ENABLE_PULSE_WAIT_MS));

    return err;
}

static esp_err_t lcd_write_nibble(hd44780_t *lcd, uint8_t nibble, uint8_t register_select)
{
    uint8_t backpack_bits = nibble | register_select | lcd->backlight_bit;
    return lcd_write_with_enable_pulse(lcd, backpack_bits);
}

static esp_err_t lcd_write_byte(hd44780_t *lcd, uint8_t byte, uint8_t register_select)
{
    esp_err_t err;

    // write high nibble
    err = lcd_write_nibble(lcd, byte & HIGH_NIBBLE_MASK, register_select);
    if (err != ESP_OK)
        return err;

    // write low nibble
    err = lcd_write_nibble(lcd, (byte << 4) & HIGH_NIBBLE_MASK, register_select);
    return err;
}

static esp_err_t lcd_send_command(hd44780_t *lcd, uint8_t command) { return lcd_write_byte(lcd, command, 0); }

static esp_err_t lcd_send_character(hd44780_t *lcd, uint8_t character)
{
    return lcd_write_byte(lcd, character, PCF8574_REGISTER_SELECT_BIT);
}

// ============================================================================
// Public API Implementation
// ============================================================================

esp_err_t hd44780_init(hd44780_t *lcd, i2c_master_bus_handle_t bus, uint8_t i2c_address, uint32_t i2c_clock_hz,
                       uint8_t columns, uint8_t rows)
{
    if (lcd == NULL) {
        ESP_LOGE(TAG, "lcd handle is null");
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_address,
        .scl_speed_hz = i2c_clock_hz,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &device_config, &lcd->device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to add lcd to i2c bus");
        return err;
    }

    lcd->columns = columns;
    lcd->rows = rows;
    lcd->backlight_bit = PCF8574_BACKLIGHT_ON_BIT;

    // wait for lcd power up
    vTaskDelay(pdMS_TO_TICKS(HD44780_POWER_ON_WAIT_MS));

    // initialize in 4-bit mode (send the wake-up nibble three times)
    lcd_write_nibble(lcd, HD44780_WAKE_UP_NIBBLE, 0);
    vTaskDelay(pdMS_TO_TICKS(HD44780_WAKE_UP_WAIT_MS));
    lcd_write_nibble(lcd, HD44780_WAKE_UP_NIBBLE, 0);
    vTaskDelay(pdMS_TO_TICKS(HD44780_WAKE_UP_WAIT_MS));
    lcd_write_nibble(lcd, HD44780_WAKE_UP_NIBBLE, 0);
    vTaskDelay(pdMS_TO_TICKS(HD44780_COMMAND_WAIT_MS));

    // set to 4-bit mode
    lcd_write_nibble(lcd, HD44780_SELECT_4_BIT_NIBBLE, 0);
    vTaskDelay(pdMS_TO_TICKS(HD44780_COMMAND_WAIT_MS));

    // function set: 4-bit mode, 2 lines, 5x8 dots
    lcd_send_command(lcd, HD44780_FUNCTION_SET | HD44780_4_BIT_BUS | HD44780_2_LINES | HD44780_5X8_FONT);

    // display control: display on
    lcd_send_command(lcd, HD44780_DISPLAY_CONTROL | HD44780_DISPLAY_ON);

    // clear display
    hd44780_clear(lcd);

    // entry mode: left to right
    lcd_send_command(lcd, HD44780_ENTRY_MODE_SET | HD44780_ENTRY_INCREMENT);

    vTaskDelay(pdMS_TO_TICKS(HD44780_INIT_SETTLE_WAIT_MS));

    ESP_LOGI(TAG, "LCD initialized at address 0x%02X (%dx%d)", i2c_address, columns, rows);
    return ESP_OK;
}

esp_err_t hd44780_clear(hd44780_t *lcd)
{
    if (lcd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = lcd_send_command(lcd, HD44780_CLEAR_DISPLAY);
    vTaskDelay(pdMS_TO_TICKS(HD44780_CLEAR_DISPLAY_WAIT_MS));
    return err;
}

esp_err_t hd44780_set_cursor(hd44780_t *lcd, uint8_t column, uint8_t row)
{
    if (lcd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (column >= lcd->columns || row >= lcd->rows) {
        ESP_LOGE(TAG, "Invalid cursor position: col=%d, row=%d", column, row);
        return ESP_ERR_INVALID_ARG;
    }

    // row offsets for different lcd sizes
    static const uint8_t ddram_row_start_addresses[] = {0x00, 0x40, 0x14, 0x54};
    uint8_t ddram_address = column + ddram_row_start_addresses[row];

    return lcd_send_command(lcd, HD44780_SET_DDRAM_ADDRESS | ddram_address);
}

esp_err_t hd44780_set_backlight(hd44780_t *lcd, bool on)
{
    if (lcd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lcd->backlight_bit = on ? PCF8574_BACKLIGHT_ON_BIT : PCF8574_BACKLIGHT_OFF;

    // send backlight state to i2c
    return i2c_master_transmit(lcd->device, &lcd->backlight_bit, sizeof(lcd->backlight_bit), PCF8574_I2C_TIMEOUT_MS);
}

esp_err_t hd44780_print(hd44780_t *lcd, const char *text)
{
    if (lcd == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    while (*text) {
        esp_err_t err = lcd_send_character(lcd, *text++);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t hd44780_printf(hd44780_t *lcd, const char *format, ...)
{
    if (lcd == NULL || format == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char formatted_text[HD44780_PRINTF_BUFFER_BYTES];
    va_list args;

    va_start(args, format);
    vsnprintf(formatted_text, sizeof(formatted_text), format, args);
    va_end(args);

    return hd44780_print(lcd, formatted_text);
}
