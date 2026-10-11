idf_component_register(
    SRCS "health.c" "health_fmt.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_system esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wunused-parameter -Wsign-compare)
