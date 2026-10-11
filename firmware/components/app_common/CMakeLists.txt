# app_types.h lives in main/ per the project layout; exporting it here lets every
# component that REQUIRES app_common share the record and event-bit definitions.
idf_component_register(
    SRCS "crc16.c" "record.c" "app_events.c" "app_ids.c" "app_nvs.c"
    INCLUDE_DIRS "include" "../../main"
    REQUIRES freertos esp_rom esp_hw_support nvs_flash)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wunused-parameter -Wsign-compare)
