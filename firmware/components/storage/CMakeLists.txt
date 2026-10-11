idf_component_register(
    SRCS "storage.c" "storage_ring.c"
    INCLUDE_DIRS "include"
    REQUIRES app_common health fatfs sdmmc esp_driver_sdmmc esp_driver_gpio esp_pm esp_timer esp_system freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wunused-parameter -Wsign-compare)
