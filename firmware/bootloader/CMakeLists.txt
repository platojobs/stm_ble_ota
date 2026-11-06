# Bootloader firmware: 32 KB at 0x08000000.

add_executable(bootloader
    src/main.c
    src/boot_flash.c
    src/boot_jump.c
    ${CMAKE_SOURCE_DIR}/firmware/vendor/stm32c5xx_dfp/Source/startup_stm32c562xx.c
    ${CMAKE_SOURCE_DIR}/firmware/vendor/stm32c5xx_dfp/Source/system_stm32c5xx.c
)

target_include_directories(bootloader PRIVATE
    ${BOOT_INCLUDES}
    ${COMMON_INCLUDES}
    ${MCU_INCLUDES}
)
target_compile_definitions(bootloader PRIVATE ${MCU_DEFINES})
target_compile_options(bootloader PRIVATE ${COMMON_WARNINGS})
target_link_libraries(bootloader PRIVATE firmware_common)

target_link_options(bootloader PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/ld/bootloader.ld
    -Wl,-Map=${CMAKE_BINARY_DIR}/bootloader.map
    -Wl,--print-memory-usage
    -specs=nano.specs
    -specs=nosys.specs
)

set_target_properties(bootloader PROPERTIES OUTPUT_NAME stm_ble_ota_bootloader)

add_custom_command(TARGET bootloader POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:bootloader> ${CMAKE_BINARY_DIR}/stm_ble_ota_bootloader.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:bootloader> ${CMAKE_BINARY_DIR}/stm_ble_ota_bootloader.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:bootloader>
    COMMENT "Generating bootloader .bin/.hex"
)
