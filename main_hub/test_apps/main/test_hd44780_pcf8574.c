/**
 * @file test_hd44780_pcf8574.c
 * @author Anthony Yalong
 * @brief LCD1602 over its PCF8574 backpack on the hub's wiring
 */

#include "unity.h"
#include "driver/i2c_master.h"
#include "hd44780_pcf8574.h"
#include "main_hub_system_config.h"

TEST_CASE("the LCD initializes, prints, and refuses an off-display cursor", "[hd44780]")
{
    i2c_master_bus_config_t bus_settings = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO_PIN,
        .scl_io_num = I2C_SCL_GPIO_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = I2C_GLITCH_FILTER_CYCLES,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_new_master_bus(&bus_settings, &bus));

    hd44780_t lcd;
    TEST_ASSERT_EQUAL(
        ESP_OK, hd44780_init(&lcd, bus, PCF8574_I2C_ADDRESS, PCF8574_I2C_CLOCK_HZ, LCD1602_COLUMNS, LCD1602_ROWS));
    TEST_ASSERT_EQUAL(ESP_OK, hd44780_print(&lcd, "sentinel test"));

    TEST_ASSERT_EQUAL(ESP_OK, hd44780_set_cursor(&lcd, LCD1602_COLUMNS - 1, LCD1602_ROWS - 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, hd44780_set_cursor(&lcd, LCD1602_COLUMNS, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, hd44780_set_cursor(&lcd, 0, LCD1602_ROWS));

    TEST_ASSERT_EQUAL(ESP_OK, i2c_master_bus_rm_device(lcd.device));
    TEST_ASSERT_EQUAL(ESP_OK, i2c_del_master_bus(bus));
}
