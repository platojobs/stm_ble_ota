// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    ota_receiver.h
 * @brief   Resumable OTA download state machine.
 *
 * Bytes stream from OTA_DATA frames into slot B. Flash pages are erased lazily
 * and the image is split into OTA_RESUME_CHUNK_SIZE (8 KB, one flash page)
 * chunks. A chunk is finalised, CRC-checked and persisted to the metadata
 * record as soon as it completes, so a power loss, BLE disconnect or transfer
 * timeout leaves a contiguous valid prefix on flash. On the next connection the
 * host announces the same manifest and the receiver verifies the persisted
 * chunks against flash, truncating to the first mismatch before accepting data
 * at the resume offset.
 *
 * The receiver is deliberately platform independent: flash access, metadata
 * persistence and progress notifications are injected, which lets the whole
 * state machine run in host unit tests.
 */

#ifndef OTA_RECEIVER_H
#define OTA_RECEIVER_H

#include "ota_protocol.h"
#include "ota_metadata.h"
#include "flash_layout.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    void *context;
    int (*flash_erase_page)(void *context, uint32_t page_address);
    int (*flash_program)(void *context, uint32_t address, const void *data,
                         uint32_t length);
    int (*flash_read)(void *context, uint32_t address, void *out, uint32_t length);
    int (*metadata_save)(void *context, const ota_metadata_t *metadata);
    void (*notify)(void *context, ota_state_t state, ota_reason_t reason,
                   uint32_t received, uint32_t total, uint32_t crc32);
} ota_receiver_io_t;

typedef struct {
    const ota_receiver_io_t *io;
    uint32_t target_base;
    uint32_t target_size;
    uint32_t hardware_id;

    ota_state_t state;
    ota_reason_t last_reason;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t image_version;
    uint32_t previous_version;
    uint32_t build_id;
    uint8_t  image_sha256[32];
    uint8_t  image_signature[64];

    uint32_t received;      /* bytes accepted from the host                    */
    uint32_t written;       /* bytes programmed to flash (16-byte aligned)     */
    uint32_t running_crc;   /* CRC-32 accumulator over received bytes          */
    uint32_t erased_pages;  /* bitmask of erased 8 KB pages inside the slot    */
    uint32_t last_activity_ms;
    uint8_t  last_progress; /* last notified percentage                        */

    /* Resume bookkeeping. */
    uint32_t chunk_count;   /* contiguous chunks whose CRC is known good       */
    uint32_t chunk_index;   /* chunk currently being filled                    */
    uint32_t chunk_crc32;   /* raw CRC accumulator for the open chunk          */
    uint32_t chunk_crcs[OTA_RESUME_MAX_CHUNKS];
    uint32_t chunk_valid;   /* bitmask of chunks verified against flash        */
    uint32_t partial_active;
    bool     resumable;     /* manifest present and partial data on flash      */

    ota_metadata_t base_meta; /* preserves running/backup fields while saving  */
    bool has_base_meta;

    uint8_t  stage[16];
    uint8_t  stage_len;
} ota_receiver_t;

/** Configure the receiver. @return 0 on success. */
int ota_receiver_init(ota_receiver_t *receiver, const ota_receiver_io_t *io,
                      uint32_t target_base, uint32_t target_size,
                      uint32_t hardware_id);

/** Tell the receiver which version currently runs in slot A (downgrade check). */
void ota_receiver_set_installed_version(ota_receiver_t *receiver, uint32_t version);

/**
 * Attach the metadata record loaded at boot. It is copied into every partial
 * save so running/backup/attempt fields are not clobbered by resume writes.
 */
void ota_receiver_attach_metadata(ota_receiver_t *receiver,
                                  const ota_metadata_t *meta);

/**
 * Restore a persisted partial transfer (state RECEIVING). Does not touch flash;
 * the chunks are re-verified when the host sends a matching BEGIN.
 */
void ota_receiver_restore(ota_receiver_t *receiver, const ota_metadata_t *meta);

/** Start or resume a transfer. Returns OTA_REASON_NONE on success. */
ota_reason_t ota_receiver_begin(ota_receiver_t *receiver,
                                const ota_begin_payload_t *begin, uint32_t now_ms);

/** Feed one DATA frame payload. Returns OTA_REASON_NONE on success. */
ota_reason_t ota_receiver_data(ota_receiver_t *receiver, uint32_t offset,
                               const uint8_t *data, uint32_t length,
                               uint32_t now_ms);

/** Finish the transfer, verify CRC/SHA-256 from flash and persist metadata. */
ota_reason_t ota_receiver_end(ota_receiver_t *receiver, const ota_end_payload_t *end,
                              uint32_t now_ms);

/** Explicit abort: discards the partial prefix, old firmware stays in slot A. */
void ota_receiver_abort(ota_receiver_t *receiver, ota_reason_t reason,
                        uint32_t now_ms);

/** Persist the current prefix after a transport loss, then report an error. */
void ota_receiver_suspend(ota_receiver_t *receiver, ota_reason_t reason,
                          uint32_t now_ms);

/** Call periodically; suspends a stalled transfer after 10 seconds of silence. */
void ota_receiver_tick(ota_receiver_t *receiver, uint32_t now_ms);

/** Percentage 0..100 of accepted bytes. */
uint8_t ota_receiver_percent(const ota_receiver_t *receiver);

/** Snapshot for the OTA_QUERY / OTA_INFO exchange. */
void ota_receiver_build_info(const ota_receiver_t *receiver,
                             ota_info_payload_t *info);

#endif /* OTA_RECEIVER_H */
