// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    ota_metadata.h
 * @brief   Upgrade state persisted across resets, read by the bootloader.
 *
 * State transitions:
 *
 *   app receives a partial image -> RECEIVING (valid chunks and running CRC
 *                                   persisted every 8 KB; a reset, timeout or
 *                                   BLE disconnect resumes from this record)
 *   app receives the full image -> READY_TO_BOOT (size/CRC/SHA-256/signature
 *                                   stored, reset)
 *   bootloader validates slot B -> INSTALLING (snapshot slot A to BACKUP,
 *                                   then copy slot B to slot A)
 *   copy verified               -> INSTALLED (slot A is the new application)
 *   validation failed           -> ERROR      (slot A untouched)
 *   three unconfirmed boots     -> rollback   (RESTORING / old image back in A)
 *
 * A reset during RECEIVING resumes at the first chunk whose CRC does not match
 * slot B. A reset during INSTALLING simply repeats the copy from intact slot B.
 * The backup record is only marked valid after its bytes and CRC have been
 * read back from BACKUP.
 */

#ifndef OTA_METADATA_H
#define OTA_METADATA_H

#include "record_store.h"
#include "ota_protocol.h"
#include "flash_layout.h"

#include <stdint.h>
#include <stdbool.h>

#define OTA_METADATA_MAGIC 0x4F4D4554u /* "OMET" */
#define OTA_VERSION_ID     2u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t layout_version;
    uint32_t state;            /* ota_state_t                                  */
    uint32_t image_size;       /* target image (slot B while downloading)      */
    uint32_t image_crc32;
    uint32_t image_version;
    uint32_t install_attempts; /* install retries of the current candidate    */
    uint32_t boot_attempts;    /* consecutive boots without APP confirmation   */
    uint32_t boot_slot;        /* FLASH_SLOT_A_START                           */
    uint32_t previous_version; /* version that was running before this upgrade */

    /* Resume bookkeeping, persisted every OTA_RESUME_CHUNK_SIZE bytes. */
    uint32_t received_bytes;   /* contiguous valid prefix in slot B            */
    uint32_t partial_crc32;    /* raw CRC-32 accumulator for that prefix        */
    uint32_t build_id;
    uint32_t hardware_id;
    uint32_t chunk_size;       /* OTA_RESUME_CHUNK_SIZE                        */
    uint32_t chunk_count;      /* number of valid chunks                       */
    uint32_t chunk_valid;      /* bitmask of chunks verified after a resume     */

    /* Rollback bookkeeping. */
    uint32_t backup_valid;
    uint32_t backup_size;
    uint32_t backup_crc32;
    uint32_t backup_version;
    uint32_t running_size;     /* image currently in slot A                    */
    uint32_t running_crc32;
    uint32_t running_version;

    uint8_t  image_sha256[32];
    uint8_t  image_signature[64];
    uint8_t  backup_sha256[32];
    uint8_t  backup_signature[64];
    uint8_t  running_sha256[32];
    uint8_t  running_signature[64];

    uint32_t chunk_crc32[OTA_RESUME_MAX_CHUNKS];
    uint32_t reserved;
} ota_metadata_t;

/** Fill a metadata structure with safe defaults. */
void ota_metadata_default(ota_metadata_t *meta);

/** @return true when magic/layout/state/resume fields are acceptable. */
bool ota_metadata_valid(const ota_metadata_t *meta);

/** Read metadata. Returns 0 on success, -1 when absent or invalid. */
int ota_metadata_read(const record_store_t *store, ota_metadata_t *out);

/** Persist metadata (fills magic/layout automatically). */
int ota_metadata_write(record_store_t *store, const ota_metadata_t *meta);

/** Read-or-default helper used by the bootloader. */
void ota_metadata_load_or_default(const record_store_t *store, ota_metadata_t *out,
                                  bool *found);

/** @return true when the image descriptor fields (size/CRC/SHA) are present. */
bool ota_metadata_image_described(const ota_metadata_t *meta);

/** Copy the backup descriptor into the active image/running descriptor. */
void ota_metadata_apply_backup(ota_metadata_t *meta);

/** Mark the currently described image as running (used after INSTALLED). */
void ota_metadata_promote_running(ota_metadata_t *meta);

#endif /* OTA_METADATA_H */
