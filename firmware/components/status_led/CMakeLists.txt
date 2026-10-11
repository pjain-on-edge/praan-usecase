idf_component_register(
    SRCS "status_led.c" "led_pattern.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio freertos
    PRIV_REQUIRES app_common esp_driver_ledc esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wunused-parameter -Wsign-compare)
