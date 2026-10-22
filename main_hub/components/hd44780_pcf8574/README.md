# HD44780 LCD Driver over a PCF8574 I²C Backpack

ESP-IDF component for an HD44780 character LCD, such as the LCD1602, driven
through a PCF8574 I²C backpack in 4-bit mode with the backlight on.

## Hardware

| Backpack Pin | Connection                                       |
| ------------ | ------------------------------------------------ |
| VCC          | 5 V                                              |
| GND          | ESP32 ground                                     |
| SDA          | The I²C data GPIO configured by the application  |
| SCL          | The I²C clock GPIO configured by the application |

The backpack answers at 0x27 when its chip is a PCF8574 and at 0x3F when it is a
PCF8574A. Three solder jumpers marked A0 to A2 can move either address, and the
address the application configures must match the board.

If the backlight is on but no characters show, or the top row is solid blocks,
turn the contrast potentiometer on the backpack.

Some backpacks pull SDA and SCL up to their own 5 V supply, which puts the bus
above what the ESP32's inputs accept. Check the board for those pull-up
resistors; if it has them, remove them and pull the bus up to 3.3 V instead, or
put a level shifter between the backpack and the ESP32.

For the hub's bus pins and the backpack's address, see `I2C_SDA_GPIO_PIN`,
`I2C_SCL_GPIO_PIN`, and `PCF8574_I2C_ADDRESS` in
[`main_hub_system_config.h`](../../main/include/main_hub_system_config.h).
