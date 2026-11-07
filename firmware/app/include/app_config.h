// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    app_config.h
 * @brief   Board pinout, firmware identity and protocol-level constants.
 *
 * The pin assignment follows the NUCLEO-C562RE Arduino header. If a custom
 * board is used, only the BSP files listed in docs/porting.md must change.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include "stm32c562xx.h"

/* Firmware identity reported by the device-info characteristic and used for
 * OTA downgrade protection. Version format matches ota_version_pack(). */
#define APP_VERSION_MAJOR       0u
#define APP_VERSION_MINOR       1u
#define APP_VERSION_PATCH       0u
#define APP_VERSION             0x00000100u  /* 0.1.0 */
#define APP_HARDWARE_ID         0x0050u      /* NUCLEO-C562RE profile */

#define APP_NAME                "stm-ble-ota"
#define BLE_DEVICE_NAME         "STM-BLE-OTA"

/* --- Pinout (NUCLEO-C562RE) --------------------------------------------- */
/* LED and user button, both present on every Nucleo board. */
#define BOARD_LED_PORT          GPIOA
#define BOARD_LED_PIN           5u
#define BOARD_BUTTON_PORT       GPIOC
#define BOARD_BUTTON_PIN        13u

/* ESP32 co-processor UART: USART1 on PA9 (TX) / PA10 (RX), AF7. */
#define ESP_UART_INSTANCE       USART1
#define ESP_UART_AF             7u
#define ESP_UART_TX_PORT        GPIOA
#define ESP_UART_TX_PIN         9u
#define ESP_UART_RX_PORT        GPIOA
#define ESP_UART_RX_PIN         10u
#define ESP_UART_BAUD           115200u
#define ESP_UART_IRQn           USART1_IRQn

/* Direct PC test link: USART2 on PA2 (TX) / PA3 (RX), AF7. This is the
 * ST-LINK virtual COM port on the Nucleo board. */
#define HOST_UART_INSTANCE      USART2
#define HOST_UART_AF            7u
#define HOST_UART_TX_PORT       GPIOA
#define HOST_UART_TX_PIN        2u
#define HOST_UART_RX_PORT       GPIOA
#define HOST_UART_RX_PIN        3u
#define HOST_UART_BAUD          115200u
#define HOST_UART_IRQn          USART2_IRQn

/* SHT30 on I2C1: PB6 (SCL) / PB7 (SDA), AF4. */
#define I2C_INSTANCE            I2C1
#define I2C_AF                  4u
#define I2C_SCL_PORT            GPIOB
#define I2C_SCL_PIN             6u
#define I2C_SDA_PORT            GPIOB
#define I2C_SDA_PIN             7u
#define SHT30_ADDRESS           0x44u

/* Light sensor on ADC1_IN0 (PA0), analog mode. */
#define LIGHT_ADC               ADC1
#define LIGHT_ADC_CHANNEL       0u
#define LIGHT_ADC_PORT          GPIOA
#define LIGHT_ADC_PIN           0u

/* --- Protocol timing ---------------------------------------------------- */
#define SENSOR_PERIOD_MS        1000u
#define WIFI_CONNECT_TIMEOUT_MS 20000u
#define ESP_COMMAND_TIMEOUT_MS  2000u
#define OTA_RESET_DELAY_MS      500u

/* --- BLE GATT profile indices ------------------------------------------- *
 * ESP-AT addresses services/characteristics by index, not by UUID. The
 * indices below are the ones the custom ESP-AT service table must declare;
 * see docs/esp32_at_custom_service.md.
 *
 * ESP-AT's gatts_data.csv custom-service path uses 16-bit UUIDs, so the
 * profile below uses an unassigned 16-bit range (0xFF00 service). If 128-bit
 * UUIDs are required, ESP-AT itself must be modified; the MCU side only needs
 * the advertisement payload and these macros updated.
 */
#define BLE_SERVICE_INDEX       1u
#define BLE_CHAR_INFO           0u
#define BLE_CHAR_SENSOR         1u
#define BLE_CHAR_PROVISION      2u
#define BLE_CHAR_OTA_CONTROL    3u
#define BLE_CHAR_OTA_DATA       4u

#define BLE_UUID_SERVICE        0xFF00u
#define BLE_UUID_CHAR_INFO      0xFF01u
#define BLE_UUID_CHAR_SENSOR    0xFF02u
#define BLE_UUID_CHAR_PROVISION 0xFF03u
#define BLE_UUID_CHAR_OTA_CTRL  0xFF04u
#define BLE_UUID_CHAR_OTA_DATA  0xFF05u

/* Advertising payload: flags (02 01 06) + complete 16-bit service UUID list
 * (03 03 00 FF). 0xFF00 is encoded little-endian. */
#define BLE_ADV_DATA            "020106030300FF"

#endif /* APP_CONFIG_H */
