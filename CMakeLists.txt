cmake_minimum_required(VERSION 3.22)

# NOTE: configure with -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake
# or use the "arm" preset (see CMakePresets.json). Host unit tests are a
# separate project under tests/ and are built with the native compiler.

project(stm_ble_ota C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# --- Firmware version injection ---------------------------------------------
# Explicit: -DFW_VERSION=1.2.3 -DFW_BUILD_ID=42
# Default: nearest git tag (vX.Y.Z), falling back to 0.0.0 outside a repo.
set(FW_VERSION "" CACHE STRING "Firmware version, semver (default: git describe)")
set(FW_BUILD_ID "" CACHE STRING "Firmware build id (default: git commit count)")
if(NOT FW_VERSION)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE _git_describe OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET RESULT_VARIABLE _git_rc)
        if(_git_rc EQUAL 0)
            string(REGEX REPLACE "^v" "" FW_VERSION "${_git_describe}")
        endif()
    endif()
endif()
if(NOT FW_VERSION)
    set(FW_VERSION "0.0.0")
endif()
if(NOT FW_BUILD_ID)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE _git_count OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET RESULT_VARIABLE _git_rc)
        if(_git_rc EQUAL 0)
            set(FW_BUILD_ID ${_git_count})
        endif()
    endif()
endif()
if(NOT FW_BUILD_ID)
    set(FW_BUILD_ID 0)
endif()
if(FW_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(FW_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(FW_VERSION_MINOR ${CMAKE_MATCH_2})
    set(FW_VERSION_PATCH ${CMAKE_MATCH_3})
else()
    message(WARNING "FW_VERSION '${FW_VERSION}' is not semver; using 0.0.0")
    set(FW_VERSION "0.0.0")
    set(FW_VERSION_MAJOR 0)
    set(FW_VERSION_MINOR 0)
    set(FW_VERSION_PATCH 0)
endif()
math(EXPR FW_VERSION_U32
    "(${FW_VERSION_MAJOR} << 16) | (${FW_VERSION_MINOR} << 8) | ${FW_VERSION_PATCH}")
message(STATUS "Firmware version ${FW_VERSION} (0x${FW_VERSION_U32}), build ${FW_BUILD_ID}")

set(MCU_DEFINES STM32C562xx)
set(MCU_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/firmware/vendor/cmsis/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/firmware/vendor/stm32c5xx_dfp/Include
)
set(COMMON_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/firmware/common/include)
set(APP_INCLUDES    ${CMAKE_CURRENT_SOURCE_DIR}/firmware/app/include)
set(BOOT_INCLUDES   ${CMAKE_CURRENT_SOURCE_DIR}/firmware/bootloader/include)

set(COMMON_WARNINGS -Wall -Wextra -Wshadow -Wdouble-promotion -Wundef)

# Optional strict mode used by CI and tools/build.sh. It is an option rather
# than CMAKE_C_FLAGS so the toolchain flags from the toolchain file survive.
option(WERROR "Treat compiler warnings as errors" OFF)
if(WERROR)
    add_compile_options(-Werror)
endif()

# TweetNaCl (public domain) is compiled as part of firmware_common so the
# bootloader can verify Ed25519 signatures. It is also used by host tests.
add_library(firmware_common STATIC
    firmware/common/src/crc.c
    firmware/common/src/sha256.c
    firmware/common/src/ed25519_verify.c
    firmware/vendor/tweetnacl/tweetnacl.c
    firmware/common/src/mini_json.c
    firmware/common/src/ota_protocol.c
    firmware/common/src/record_store.c
    firmware/common/src/config_store.c
    firmware/common/src/ota_metadata.c
    firmware/common/src/image_check.c
    firmware/common/src/ota_receiver.c
    firmware/common/src/boot_install.c
    firmware/common/src/boot_policy.c
    firmware/common/src/cli_parser.c
    firmware/common/src/sensor_filter.c
    firmware/common/src/esp_at_parser.c
    firmware/common/src/hex_util.c
    firmware/common/src/err_code.c
)
target_include_directories(firmware_common PUBLIC
    ${COMMON_INCLUDES}
    ${MCU_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/firmware/vendor/tweetnacl
)
target_compile_definitions(firmware_common PUBLIC ${MCU_DEFINES})
target_compile_options(firmware_common PRIVATE ${COMMON_WARNINGS})
# TweetNaCl is vendored public-domain code; keep project warnings off it.
set_source_files_properties(firmware/vendor/tweetnacl/tweetnacl.c
    PROPERTIES COMPILE_OPTIONS "-w")

add_subdirectory(firmware/bootloader)
add_subdirectory(firmware/app)
