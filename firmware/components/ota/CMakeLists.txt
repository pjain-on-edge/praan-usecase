idf_component_register(
    SRCS "ota.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_common storage esp_https_ota esp_http_client app_update mbedtls freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wunused-parameter -Wsign-compare)
