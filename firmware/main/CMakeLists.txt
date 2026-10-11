idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES app_common health sensor_mgr storage net publisher status_led button
             nvs_flash esp_pm esp_timer bt esp_driver_gpio esp_driver_uart)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wunused-parameter -Wsign-compare)
