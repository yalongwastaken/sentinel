set(hub_components "${CMAKE_CURRENT_SOURCE_DIR}/../../main_hub/components")

idf_component_register(
    SRCS "test_main.c" "test_dht11_frame.c" "test_hcsr04_echo.c"
         "${hub_components}/dht11/dht11_frame.c"
         "${hub_components}/hcsr04/hcsr04_echo.c"
    INCLUDE_DIRS "${hub_components}/dht11/include" "${hub_components}/hcsr04/include"
    PRIV_REQUIRES unity
    WHOLE_ARCHIVE
)
