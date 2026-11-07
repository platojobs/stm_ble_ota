// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    ota_metadata.h
 * @brief   Upgrade state persisted across resets, read by the bootloader.
 *
 * State transitions:
 *
 *   app receives image  -> DOWNLOAD_READY (size/crc/version stored, reset)
 *   bootloader validates slot B -> INSTALLING (erase + copy slot B to slot A)
 *   copy verified       -> INSTALLED    (slot A is the new application)
 *   validation failed   -> FAILED       (slot A untouched, old app still boots)
 *
 * A reset during INSTALLING is recovered on the next boot: the copy is simply
 * repeated from intact slot B.
 */

#ifndef OTA_METADATA_H
#define OTA_METADATA_H

#include "record_store.h"
#include "ota_protocol.h"

#include <stdint.h>
#include <stdbool.h>

#define OTA_METADATA_MAGIC 0x4F4D4554u /* "OMET" */
#define OTA_VERSION_ID     1u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t layout_version;
    uint32_t state;            /* ota_state_t */
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t image_version;
    uint32_t install_attempts;
    uint32_t boot_slot;        /* FLASH_SLOT_A_START */
    uint32_t previous_version; /* version currently running in slot A          */
    uint32_t reserved;
} ota_metadata_t;

/** Fill a metadata structure with safe defaults. */
void ota_metadata_default(ota_metadata_t *meta);

/** @return true when magic/layout/state are acceptable. */
bool ota_metadata_valid(const ota_metadata_t *meta);

/** Read metadata. Returns 0 on success, -1 when absent or invalid. */
int ota_metadata_read(const record_store_t *store, ota_metadata_t *out);

/** Persist metadata (fills magic/layout automatically). */
int ota_metadata_write(record_store_t *store, const ota_metadata_t *meta);

/** Read-or-default helper used by the bootloader. */
void ota_metadata_load_or_default(const record_store_t *store, ota_metadata_t *out,
                                  bool *found);

#endif /* OTA_METADATA_H */
