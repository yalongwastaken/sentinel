/**
 * @file test_lcd_i2c.c
 * @author Anthony Yalong
 * @brief I2C LCD display test - validates LCD initialization, text display,
 *        and cursor positioning. Replace main/main.c with this file.
 */

// imports
#include "lcd_i2c.h"
#include "main_hub_system_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// test configuration
#define TASK_DELAY_MS 1000 // 1s task delay
#define TASK_STACK_DEPTH 4096
#define TASK_PRIORITY 5

// logging
static const char *TAG = "test_lcd_i2c";

// lcd handle
static lcd_handle_t lcd;
static uint32_t counter = 0;

// function prototypes
esp_err_t i2c_master_init(i2c_master_bus_handle_t *bus);
void test_lcd_i2c(void *pvParameters);

void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Starting LCD I2C test...");

    // initialize i2c
    i2c_master_bus_handle_t i2c_bus;
    ret = i2c_master_init(&i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize i2c master");
        return;
    }

    // initialize lcd
    ret = lcd_init(&lcd, i2c_bus, LCD_ADDR, I2C_MASTER_FREQ_HZ, LCD_COLUMNS, LCD_ROWS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize lcd");
        return;
    }

    // display startup message
    lcd_set_cursor(&lcd, 0, 0);
    lcd_print(&lcd, "lcd test");
    lcd_set_cursor(&lcd, 0, 1);
    lcd_print(&lcd, "initializing...");

    vTaskDelay(pdMS_TO_TICKS(2000));

    // create lcd task
    xTaskCreate(test_lcd_i2c, "test_lcd_i2c", TASK_STACK_DEPTH, NULL, TASK_PRIORITY, NULL);
}

esp_err_t i2c_master_init(i2c_master_bus_handle_t *bus)
{
    // i2c config
    i2c_master_bus_config_t config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = I2C_MASTER_GLITCH_FILTER_CYCLES,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&config, bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to create i2c master bus");
        return ret;
    }

    ESP_LOGI(TAG, "i2c master initialized");
    return ESP_OK;
}

void test_lcd_i2c(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();

    lcd_backlight(&lcd, true);

    while (true) {
        // update display
        lcd_clear(&lcd);

        // line 1: counter
        lcd_set_cursor(&lcd, 0, 0);
        lcd_printf(&lcd, "count: %lu", counter);

        // line 2: message
        lcd_set_cursor(&lcd, 0, 1);
        lcd_print(&lcd, "lcd working!");

        ESP_LOGI(TAG, "display updated - counter: %lu", counter);

        counter++;

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_DELAY_MS));
    }

    vTaskDelete(NULL);
}