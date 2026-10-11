idf_component_register(
    SRCS "sensor_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES app_common health drv_sht30 drv_bme280 drv_ds3231 drv_pms5003
             esp_driver_i2c esp_driver_gpio esp_timer esp_pm esp_system freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wunused-parameter -Wsign-compare)
