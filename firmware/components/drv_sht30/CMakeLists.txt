idf_component_register(
    SRCS "sht30.c" "sht30_conv.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wunused-parameter -Wsign-compare)
