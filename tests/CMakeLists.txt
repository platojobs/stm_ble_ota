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
option(WERROR "Treat compiler warnings as errors" OFF)
if(WERROR)
    add_compile_options(-Werror)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/common)
set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/vendor)
set(COMMON_SOURCES
    ${COMMON_DIR}/src/crc.c
    ${COMMON_DIR}/src/sha256.c
    ${COMMON_DIR}/src/ed25519_verify.c
    ${VENDOR_DIR}/tweetnacl/tweetnacl.c
    ${COMMON_DIR}/src/mini_json.c
    ${COMMON_DIR}/src/hex_util.c
    ${COMMON_DIR}/src/ota_protocol.c
    ${COMMON_DIR}/src/record_store.c
    ${COMMON_DIR}/src/config_store.c
    ${COMMON_DIR}/src/ota_metadata.c
    ${COMMON_DIR}/src/image_check.c
    ${COMMON_DIR}/src/ota_receiver.c
    ${COMMON_DIR}/src/boot_install.c
    ${COMMON_DIR}/src/boot_policy.c
    ${COMMON_DIR}/src/cli_parser.c
    ${COMMON_DIR}/src/sensor_filter.c
    ${COMMON_DIR}/src/esp_at_parser.c
)

add_executable(unit_tests
    ${COMMON_SOURCES}
    fake_flash.c
    test_common.c
    test_ota_logic.c
    test_v2_features.c
    test_main.c
)

target_include_directories(unit_tests PRIVATE
    ${COMMON_DIR}/include
    ${VENDOR_DIR}/tweetnacl
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(unit_tests PRIVATE
    -Wall -Wextra -Wshadow -Wdouble-promotion -Wundef
    -fsanitize=address,undefined -fno-omit-frame-pointer
)

target_link_options(unit_tests PRIVATE -fsanitize=address,undefined)
target_link_libraries(unit_tests PRIVATE m)

# Vendored public-domain crypto: compile without project warnings.
set_source_files_properties(${VENDOR_DIR}/tweetnacl/tweetnacl.c
    PROPERTIES COMPILE_OPTIONS "-w")
