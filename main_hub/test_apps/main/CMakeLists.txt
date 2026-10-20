idf_component_register(
    SRCS "test_main.c" "test_hcsr501.c" "test_hcsr04.c" "test_dht11.c" "test_hd44780_pcf8574.c"
    INCLUDE_DIRS "../../main/include"
    PRIV_REQUIRES unity hcsr501 hcsr04 dht11 hd44780_pcf8574 esp_driver_gpio esp_driver_i2c esp_timer
    WHOLE_ARCHIVE
)
