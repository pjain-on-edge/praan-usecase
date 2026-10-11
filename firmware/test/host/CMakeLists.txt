cmake_minimum_required(VERSION 3.16)
project(praan_host_tests C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_EXTENSIONS ON)

include(FetchContent)
FetchContent_Declare(unity
    URL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.6.0.tar.gz
    URL_HASH SHA256=aa4c9fb1ae5fc5242f914c65f3557e817e40cb37f04a31e5ff76d1ab89dbf674
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(unity)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(C ${ROOT}/components)

# Only the hardware-independent sources; none of them include ESP-IDF headers.
add_library(pure STATIC
    ${C}/app_common/crc16.c
    ${C}/app_common/record.c
    ${C}/drv_sht30/sht30_conv.c
    ${C}/drv_bme280/bme280_comp.c
    ${C}/drv_ds3231/ds3231_time.c
    ${C}/drv_pms5003/pms_parse.c
    ${C}/storage/storage_ring.c
    ${C}/publisher/pub_encode.c
    ${C}/publisher/pub_inflight.c
    ${C}/net/net_backoff.c
    ${C}/button/button_sm.c
    ${C}/status_led/led_pattern.c
    ${C}/health/health_fmt.c)
target_include_directories(pure PUBLIC
    ${ROOT}/main
    ${C}/app_common/include ${C}/drv_sht30/include ${C}/drv_bme280/include
    ${C}/drv_ds3231/include ${C}/drv_pms5003/include ${C}/storage/include
    ${C}/publisher/include ${C}/net/include ${C}/button/include
    ${C}/status_led/include ${C}/health/include)
target_compile_options(pure PRIVATE -Wall -Wextra -Werror)

enable_testing()
foreach(t crc sht30 pms bme280 ds3231 pub_encode ring inflight misc)
    add_executable(test_${t} test_${t}.c)
    target_link_libraries(test_${t} PRIVATE pure unity)
    target_compile_options(test_${t} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${t} COMMAND test_${t})
endforeach()
