# Application firmware: 96 KB in slot A at 0x08008000.
#
# Layer order: bsp -> drivers -> services -> app. Only app/main.c wires
# services together; services depend on drivers and BSP, never the reverse.

add_executable(stm_ble_ota_app
    src/app/main.c
    src/app/host_link.c
    src/bsp/bsp_time.c
    src/bsp/bsp_gpio.c
    src/bsp/bsp_uart.c
    src/bsp/bsp_i2c.c
    src/bsp/bsp_adc.c
    src/bsp/bsp_flash.c
    src/bsp/bsp_watchdog.c
    src/drivers/sht30.c
    src/drivers/esp32_at.c
    src/services/sensor_service.c
    src/services/wifi_service.c
    src/services/ble_service.c
    src/services/ota_service.c
    src/services/cli_service.c
    ${CMAKE_SOURCE_DIR}/firmware/common/src/syscalls.c
    ${CMAKE_SOURCE_DIR}/firmware/vendor/stm32c5xx_dfp/Source/startup_stm32c562xx.c
    ${CMAKE_SOURCE_DIR}/firmware/vendor/stm32c5xx_dfp/Source/system_stm32c5xx.c
)

target_include_directories(stm_ble_ota_app PRIVATE
    ${APP_INCLUDES}
    ${COMMON_INCLUDES}
    ${MCU_INCLUDES}
)
target_compile_definitions(stm_ble_ota_app PRIVATE
    ${MCU_DEFINES}
    APP_VERSION_STRING="${FW_VERSION}"
    APP_VERSION=${FW_VERSION_U32}u
    APP_VERSION_MAJOR=${FW_VERSION_MAJOR}u
    APP_VERSION_MINOR=${FW_VERSION_MINOR}u
    APP_VERSION_PATCH=${FW_VERSION_PATCH}u
    APP_BUILD_ID=${FW_BUILD_ID}u
)
target_compile_options(stm_ble_ota_app PRIVATE ${COMMON_WARNINGS})
target_link_libraries(stm_ble_ota_app PRIVATE firmware_common)

target_link_options(stm_ble_ota_app PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/ld/app.ld
    -Wl,-Map=${CMAKE_BINARY_DIR}/stm_ble_ota_app.map
    -Wl,--print-memory-usage
    -specs=nano.specs
    -specs=nosys.specs
)

set_target_properties(stm_ble_ota_app PROPERTIES OUTPUT_NAME stm_ble_ota_app)

add_custom_command(TARGET stm_ble_ota_app POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:stm_ble_ota_app> ${CMAKE_BINARY_DIR}/stm_ble_ota_app.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:stm_ble_ota_app> ${CMAKE_BINARY_DIR}/stm_ble_ota_app.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:stm_ble_ota_app>
    COMMENT "Generating application .bin/.hex"
)
