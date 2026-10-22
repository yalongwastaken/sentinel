idf_component_register(
    SRCS "crash_limit.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_app_format
)
