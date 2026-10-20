idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "include"
    REQUIRES nvs_flash bt hcsr501 hcsr04 dht11 hd44780_pcf8574 crash_limit esp_driver_gpio esp_driver_i2c
)
