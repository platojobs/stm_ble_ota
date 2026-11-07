cmake_minimum_required(VERSION 3.22)

# NOTE: configure with -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake
# or use the "arm" preset (see CMakePresets.json). Host unit tests are a
# separate project under tests/ and are built with the native compiler.

project(stm_ble_ota C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(MCU_DEFINES STM32C562xx)
set(MCU_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/firmware/vendor/cmsis/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/firmware/vendor/stm32c5xx_dfp/Include
)
set(COMMON_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/firmware/common/include)
set(APP_INCLUDES    ${CMAKE_CURRENT_SOURCE_DIR}/firmware/app/include)
set(BOOT_INCLUDES   ${CMAKE_CURRENT_SOURCE_DIR}/firmware/bootloader/include)

set(COMMON_WARNINGS -Wall -Wextra -Wshadow -Wdouble-promotion -Wundef)

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
