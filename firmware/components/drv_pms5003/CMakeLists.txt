idf_component_register(
    SRCS "pms5003.c" "pms_parse.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart esp_driver_gpio esp_timer freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wunused-parameter -Wsign-compare)
