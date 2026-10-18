idf_component_register(
    SRCS "hd44780_pcf8574.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
)