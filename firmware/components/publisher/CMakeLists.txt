idf_component_register(
    SRCS "publisher.c" "pub_cmd.c" "pub_encode.c" "pub_inflight.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES app_common
    PRIV_REQUIRES storage health net ota status_led mqtt json esp_app_format esp_timer esp_system mbedtls freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wunused-parameter -Wsign-compare)
