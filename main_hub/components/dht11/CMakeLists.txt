idf_component_register(
    SRCS "dht11.c" "dht11_frame.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_timer
)
