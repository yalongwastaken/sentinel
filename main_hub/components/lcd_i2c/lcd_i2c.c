/**
 * @file lcd_i2c.c
 * @author Anthony Yalong
 * @brief I2C LCD1602 display driver implementation
 */

#include "lcd_i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdarg.h>
#include <stdio.h>

static const char *TAG = "LCD_I2C";

// ============================================================================
// Helper Functions
// ============================================================================

static esp_err_t lcd_pulse_enable(lcd_handle_t *lcd, uint8_t data)
{
    esp_err_t err;
    uint8_t byte;

    // pulse enable high
    byte = data | LCD_EN;
    err = i2c_master_transmit(lcd->device, &byte, sizeof(byte), I2C_TIMEOUT_MS);

    vTaskDelay(pdMS_TO_TICKS(1));

    // pulse enable low
    byte = data & ~LCD_EN;
    err = i2c_master_transmit(lcd->device, &byte, sizeof(byte), I2C_TIMEOUT_MS);

    vTaskDelay(pdMS_TO_TICKS(1));

    return err;
}

static esp_err_t lcd_write_nibble(lcd_handle_t *lcd, uint8_t nibble, uint8_t mode)
{
    uint8_t data = nibble | mode | lcd->backlight_state;
    return lcd_pulse_enable(lcd, data);
}

static esp_err_t lcd_write_byte(lcd_handle_t *lcd, uint8_t data, uint8_t mode)
{
    esp_err_t err;

    // write high nibble
    err = lcd_write_nibble(lcd, data & 0xF0, mode);
    if (err != ESP_OK)
        return err;

    // write low nibble
    err = lcd_write_nibble(lcd, (data << 4) & 0xF0, mode);
    return err;
}

static esp_err_t lcd_send_command(lcd_handle_t *lcd, uint8_t cmd) { return lcd_write_byte(lcd, cmd, 0); }

static esp_err_t lcd_send_data(lcd_handle_t *lcd, uint8_t data) { return lcd_write_byte(lcd, data, LCD_RS); }

// ============================================================================
// Public API Implementation
// ============================================================================

esp_err_t lcd_init(lcd_handle_t *lcd, i2c_master_bus_handle_t bus, uint8_t addr, uint32_t scl_speed_hz, uint8_t cols,
                   uint8_t rows)
{
    if (lcd == NULL) {
        ESP_LOGE(TAG, "lcd handle is null");
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = scl_speed_hz,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &device_config, &lcd->device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to add lcd to i2c bus");
        return err;
    }

    lcd->cols = cols;
    lcd->rows = rows;
    lcd->backlight_state = LCD_BACKLIGHT;

    // wait for lcd power up
    vTaskDelay(pdMS_TO_TICKS(50));

    // initialize in 4-bit mode (send 0x30 three times)
    lcd_write_nibble(lcd, 0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(lcd, 0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(lcd, 0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    // set to 4-bit mode
    lcd_write_nibble(lcd, 0x20, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    // function set: 4-bit mode, 2 lines, 5x8 dots
    lcd_send_command(lcd, LCD_CMD_FUNCTION_SET | LCD_4BIT_MODE | LCD_2_LINE | LCD_5x8_DOTS);

    // display control: display on
    lcd_send_command(lcd, LCD_CMD_DISPLAY_CTRL | LCD_DISPLAY_ON);

    // clear display
    lcd_clear(lcd);

    // entry mode: left to right
    lcd_send_command(lcd, LCD_CMD_ENTRY_MODE | LCD_ENTRY_LEFT);

    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "LCD initialized at address 0x%02X (%dx%d)", addr, cols, rows);
    return ESP_OK;
}

esp_err_t lcd_clear(lcd_handle_t *lcd)
{
    if (lcd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = lcd_send_command(lcd, LCD_CMD_CLEAR);
    vTaskDelay(pdMS_TO_TICKS(2));
    return err;
}

esp_err_t lcd_set_cursor(lcd_handle_t *lcd, uint8_t col, uint8_t row)
{
    if (lcd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (col >= lcd->cols || row >= lcd->rows) {
        ESP_LOGE(TAG, "Invalid cursor position: col=%d, row=%d", col, row);
        return ESP_ERR_INVALID_ARG;
    }

    // row offsets for different lcd sizes
    static const uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
    uint8_t addr = col + row_offsets[row];

    return lcd_send_command(lcd, LCD_CMD_DDRAM_ADDR | addr);
}

esp_err_t lcd_backlight(lcd_handle_t *lcd, bool state)
{
    if (lcd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lcd->backlight_state = state ? LCD_BACKLIGHT : LCD_NO_BACKLIGHT;

    // send backlight state to i2c
    return i2c_master_transmit(lcd->device, &lcd->backlight_state, sizeof(lcd->backlight_state), I2C_TIMEOUT_MS);
}

esp_err_t lcd_print(lcd_handle_t *lcd, const char *str)
{
    if (lcd == NULL || str == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    while (*str) {
        esp_err_t err = lcd_send_data(lcd, *str++);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t lcd_printf(lcd_handle_t *lcd, const char *format, ...)
{
    if (lcd == NULL || format == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char buffer[64];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    return lcd_print(lcd, buffer);
}