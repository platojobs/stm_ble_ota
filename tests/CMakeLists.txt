# Host-side unit tests.
#
# These build the platform-independent firmware/common modules with the native
# compiler and run them with AddressSanitizer/UBSan. The target firmware is a
# separate CMake project; this one is deliberately independent so it can run on
# a laptop without any ARM toolchain.
#
#   cmake -S tests -B build-host -G Ninja
#   cmake --build build-host
#   ./build-host/unit_tests

cmake_minimum_required(VERSION 3.22)
project(stm_ble_ota_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/common)
set(COMMON_SOURCES
    ${COMMON_DIR}/src/crc.c
    ${COMMON_DIR}/src/mini_json.c
    ${COMMON_DIR}/src/hex_util.c
    ${COMMON_DIR}/src/ota_protocol.c
    ${COMMON_DIR}/src/record_store.c
    ${COMMON_DIR}/src/config_store.c
    ${COMMON_DIR}/src/ota_metadata.c
    ${COMMON_DIR}/src/image_check.c
    ${COMMON_DIR}/src/ota_receiver.c
    ${COMMON_DIR}/src/boot_install.c
    ${COMMON_DIR}/src/sensor_filter.c
    ${COMMON_DIR}/src/esp_at_parser.c
)

add_executable(unit_tests
    ${COMMON_SOURCES}
    fake_flash.c
    test_common.c
    test_ota_logic.c
    test_main.c
)

target_include_directories(unit_tests PRIVATE
    ${COMMON_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(unit_tests PRIVATE
    -Wall -Wextra -Wshadow -Wdouble-promotion -Wundef
    -fsanitize=address,undefined -fno-omit-frame-pointer
)

target_link_options(unit_tests PRIVATE -fsanitize=address,undefined)
target_link_libraries(unit_tests PRIVATE m)
