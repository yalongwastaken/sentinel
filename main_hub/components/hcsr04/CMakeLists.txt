idf_component_register(
    SRCS "hcsr04.c" "hcsr04_echo.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_timer
)
