idf_component_register(
    SRCS "net.c" "net_backoff.c"
    INCLUDE_DIRS "include"
    REQUIRES app_common esp_wifi esp_netif esp_event esp_timer wifi_provisioning)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wunused-parameter -Wsign-compare)
