idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "include"
    REQUIRES nvs_flash bt pir hcsr04 dht11 lcd_i2c driver esp_driver_gpio
)
