// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    ota_receiver.h
 * @brief   Platform-independent OTA download state machine.
 *
 * The receiver streams firmware bytes from 16-byte OTA_DATA frames into the
 * inactive slot (slot B), erasing 8 KB pages lazily, and finally verifies the
 * CRC from flash before persisting a DOWNLOAD_READY metadata record.
 *
 * Flash access, metadata persistence and progress notifications are injected,
 * so the whole state machine is exercised by host unit tests.
 */

#ifndef OTA_RECEIVER_H
#define OTA_RECEIVER_H

#include "ota_protocol.h"
#include "ota_metadata.h"

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

    uint32_t received;      /* bytes accepted from the host                    */
    uint32_t written;       /* bytes programmed to flash (16-byte aligned)     */
    uint32_t running_crc;   /* CRC-32 accumulator over received bytes          */
    uint32_t erased_pages;  /* bitmask of erased 8 KB pages inside the slot    */
    uint32_t last_activity_ms;
    uint8_t  last_progress; /* last notified percentage                        */

    uint8_t  stage[16];
    uint8_t  stage_len;
} ota_receiver_t;

/** Configure the receiver. @return 0 on success. */
int ota_receiver_init(ota_receiver_t *receiver, const ota_receiver_io_t *io,
                      uint32_t target_base, uint32_t target_size,
                      uint32_t hardware_id);

/** Tell the receiver which version currently runs in slot A (for downgrade checks). */
void ota_receiver_set_installed_version(ota_receiver_t *receiver, uint32_t version);

/** Start a transfer. Returns OTA_REASON_NONE on success. */
ota_reason_t ota_receiver_begin(ota_receiver_t *receiver, const ota_begin_payload_t *begin,
                                uint32_t now_ms);

/** Feed one DATA frame payload. Returns OTA_REASON_NONE on success. */
ota_reason_t ota_receiver_data(ota_receiver_t *receiver, uint32_t offset,
                               const uint8_t *data, uint32_t length, uint32_t now_ms);

/** Finish the transfer, verify and persist metadata. */
ota_reason_t ota_receiver_end(ota_receiver_t *receiver, const ota_end_payload_t *end,
                              uint32_t now_ms);

/** Abort an active transfer. */
void ota_receiver_abort(ota_receiver_t *receiver, ota_reason_t reason, uint32_t now_ms);

/** Call periodically; aborts a stalled transfer after 10 seconds of silence. */
void ota_receiver_tick(ota_receiver_t *receiver, uint32_t now_ms);

/** Percentage 0..100 of accepted bytes. */
uint8_t ota_receiver_percent(const ota_receiver_t *receiver);

#endif /* OTA_RECEIVER_H */
