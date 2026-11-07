// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    err_code.h
 * @brief   Single, stable error-code space shared by the bootloader, the
 *          application services, the CLI and the host tooling.
 *
 * Codes are 16 bits wide and grouped by domain:
 *
 *   0x0000        success
 *   0x01xx        arguments, framing and protocol
 *   0x02xx        flash and persisted storage
 *   0x03xx        OTA transfer state machine
 *   0x04xx        image validation (vector table, CRC, SHA-256, signature)
 *   0x05xx        network / ESP-AT / WiFi / BLE
 *   0x06xx        sensors and buses
 *   0x07xx        system, watchdog and boot policy
 *
 * The numeric value is part of the wire/CLI contract: it is written to the
 * error log and reported as `err_code` in JSON. Do not renumber existing
 * entries; append a new one and update docs/error_codes.md.
 *
 * Domain enums (ota_reason_t, image_check_result_t) remain the internal
 * vocabulary of their modules; the mapping functions below translate them
 * into the shared space.
 */

#ifndef ERR_CODE_H
#define ERR_CODE_H

#include "ota_protocol.h"
#include "image_check.h"

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ERR_CODE_OK = 0x0000,

    /* 0x01xx: argument and protocol errors. */
    ERR_CODE_INVALID_ARG       = 0x0101,
    ERR_CODE_NULL_POINTER      = 0x0102,
    ERR_CODE_BUFFER_TOO_SMALL  = 0x0103,
    ERR_CODE_MALFORMED_FRAME   = 0x0104,
    ERR_CODE_BAD_CRC           = 0x0105,
    ERR_CODE_UNSUPPORTED       = 0x0106,
    ERR_CODE_LINE_TOO_LONG     = 0x0107,

    /* 0x02xx: flash and record store. */
    ERR_CODE_FLASH_ERASE       = 0x0201,
    ERR_CODE_FLASH_PROGRAM     = 0x0202,
    ERR_CODE_FLASH_READ        = 0x0203,
    ERR_CODE_STORE_EMPTY       = 0x0204,
    ERR_CODE_STORE_CORRUPT     = 0x0205,
    ERR_CODE_STORE_FULL        = 0x0206,

    /* 0x03xx: OTA transfer state machine. */
    ERR_CODE_OTA_BAD_STATE     = 0x0301,
    ERR_CODE_OTA_BAD_SIZE      = 0x0302,
    ERR_CODE_OTA_BAD_CRC       = 0x0303,
    ERR_CODE_OTA_OUT_OF_ORDER  = 0x0304,
    ERR_CODE_OTA_TIMEOUT       = 0x0305,
    ERR_CODE_OTA_ABORTED       = 0x0306,
    ERR_CODE_OTA_BAD_VERSION   = 0x0307,

    /* 0x04xx: image validation. */
    ERR_CODE_IMAGE_SIZE        = 0x0401,
    ERR_CODE_IMAGE_VECTOR      = 0x0402,
    ERR_CODE_IMAGE_CRC         = 0x0403,
    ERR_CODE_IMAGE_SHA256      = 0x0404,
    ERR_CODE_IMAGE_SIGNATURE   = 0x0405,

    /* 0x05xx: network and wireless. */
    ERR_CODE_AT_TIMEOUT        = 0x0501,
    ERR_CODE_AT_ERROR          = 0x0502,
    ERR_CODE_WIFI_NOT_PROVISIONED = 0x0503,
    ERR_CODE_WIFI_CONNECT_FAILED  = 0x0504,
    ERR_CODE_BLE_UNAVAILABLE   = 0x0505,

    /* 0x06xx: sensors and buses. */
    ERR_CODE_I2C_ERROR         = 0x0601,
    ERR_CODE_SENSOR_BUSY       = 0x0602,
    ERR_CODE_SENSOR_RANGE      = 0x0603,

    /* 0x07xx: system, watchdog and boot policy. */
    ERR_CODE_WATCHDOG_RESET    = 0x0701,
    ERR_CODE_BOOT_FAILED       = 0x0702,
    ERR_CODE_ROLLBACK          = 0x0703,
    ERR_CODE_NOT_IMPLEMENTED   = 0x0704,
    ERR_CODE_SELFTEST_FAILED   = 0x0705,
} err_code_t;

/** Translate an OTA reason code into the shared error space. */
err_code_t err_code_from_ota_reason(ota_reason_t reason);

/** Translate an image validation result into the shared error space. */
err_code_t err_code_from_image_result(image_check_result_t result);

/** Stable, human-readable identifier ("OTA_BAD_SIZE", "OK", ...). */
const char *err_code_name(err_code_t code);

/** Short category name derived from the high byte ("ota", "flash", ...). */
const char *err_code_domain_name(err_code_t code);

/** @return true when @p code is a known non-zero code. */
bool err_code_is_known(err_code_t code);

#endif /* ERR_CODE_H */
