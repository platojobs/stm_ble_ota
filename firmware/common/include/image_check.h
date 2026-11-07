// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    image_check.h
 * @brief   Validation of a Cortex-M application image stored in flash.
 */

#ifndef IMAGE_CHECK_H
#define IMAGE_CHECK_H

#include "ota_protocol.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    void *context;
    int (*read)(void *context, uint32_t address, void *out, uint32_t len);
} image_flash_if_t;

typedef enum {
    IMAGE_OK = 0,
    IMAGE_ERR_SIZE = 1,
    IMAGE_ERR_VECTOR = 2,   /* initial SP or reset vector outside expectations */
    IMAGE_ERR_CRC = 3,
    IMAGE_ERR_FLASH = 4,
    IMAGE_ERR_SHA256 = 5,
    IMAGE_ERR_SIGNATURE = 6,
} image_check_result_t;

/**
 * Descriptor for an authenticated image check. @c sha256 and @c signature may
 * be NULL for a factory image that predates OTA metadata; the bootloader then
 * falls back to size/vector/CRC validation.
 */
typedef struct {
    uint32_t image_size;
    uint32_t expected_crc;
    uint32_t link_base;
    const uint8_t *sha256;                /* 32 bytes, or NULL                */
    const ota_signature_blob_t *signed_blob; /* NULL when no signature         */
    const uint8_t *signature;             /* 64 bytes, or NULL                */
} image_expectation_t;

/**
 * Fully validate an image: size bounds, vector table sanity and CRC-32.
 * @param storage_base address where the bytes currently live (slot A or B).
 * @param slot_size    maximum image size accepted.
 * @param image_size   number of bytes the image occupies (CRC covers exactly this).
 * @param expected_crc CRC-32/IEEE of the @p image_size bytes.
 * @param link_base    address the image was linked for. The image downloaded to
 *                     slot B still runs from slot A after installation, so its
 *                     reset vector points into slot A, not into slot B.
 */
image_check_result_t image_validate(const image_flash_if_t *flash_if,
                                    uint32_t storage_base,
                                    uint32_t slot_size,
                                    uint32_t image_size,
                                    uint32_t expected_crc,
                                    uint32_t link_base);

/**
 * Heuristic check used for a factory image flashed with ST-Link, where no size
 * or CRC metadata exists yet. Verifies the initial stack pointer and reset
 * vector only.
 */
image_check_result_t image_probe(const image_flash_if_t *flash_if,
                                 uint32_t base,
                                 uint32_t slot_size);

/**
 * Validate an image with CRC-32, SHA-256 and an optional Ed25519 signature over
 * the canonical descriptor blob. Reads the image once, hashing and checksumming
 * the same bytes that the vector check accepted.
 */
image_check_result_t image_validate_full(const image_flash_if_t *flash_if,
                                         uint32_t storage_base,
                                         uint32_t slot_size,
                                         const image_expectation_t *expect);

#endif /* IMAGE_CHECK_H */
