set(hub_components "${CMAKE_CURRENT_SOURCE_DIR}/../../main_hub/components")
set(shared_components "${CMAKE_CURRENT_SOURCE_DIR}/../../components")

idf_component_register(
    SRCS "test_main.c" "test_dht11_frame.c" "test_hcsr04_echo.c" "test_remote_motion_ble.c"
         "${hub_components}/dht11/dht11_frame.c"
         "${hub_components}/hcsr04/hcsr04_echo.c"
         "${shared_components}/remote_motion_ble/remote_motion_ble.c"
    INCLUDE_DIRS "${hub_components}/dht11/include" "${hub_components}/hcsr04/include"
                 "${shared_components}/remote_motion_ble/include"
    PRIV_REQUIRES unity
    WHOLE_ARCHIVE
)
