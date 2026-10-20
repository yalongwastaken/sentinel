idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "include"
    REQUIRES nvs_flash bt hcsr501 crash_limit
)
