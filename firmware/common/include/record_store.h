// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    record_store.h
 * @brief   Power-loss-safe key/value record stored in two alternating flash
 *          pages.
 *
 * One record occupies one entire 8 KB page. Saving always writes to the page
 * that does *not* hold the newest valid record. A torn write therefore leaves
 * the previous record intact; the loader picks the valid record with the
 * highest sequence number.
 *
 * The flash interface is injected so the same logic runs on the target and in
 * host unit tests.
 */

#ifndef RECORD_STORE_H
#define RECORD_STORE_H

#include <stdint.h>
#include <stdbool.h>

#define RECORD_MAGIC      0x52454331u /* "REC1" */
#define RECORD_HEADER_LEN 16u
#define RECORD_MAX_PAYLOAD (0x2000u - RECORD_HEADER_LEN)

typedef struct {
    void *context;
    /** Erase one 8 KB page. Return 0 on success. */
    int (*erase_page)(void *context, uint32_t page_address);
    /**
     * Program bytes at @p address. Implementations may require 16-byte
     * granularity; the record is padded with 0xFF before this call.
     */
    int (*program)(void *context, uint32_t address, const void *data, uint32_t len);
    /** Read bytes from flash. Return 0 on success. */
    int (*read)(void *context, uint32_t address, void *out, uint32_t len);
} record_flash_if_t;

typedef struct {
    const record_flash_if_t *flash;
    uint32_t page_a;
    uint32_t page_b;
    uint16_t max_payload;
} record_store_t;

typedef struct {
    uint32_t sequence;
    uint32_t payload_len;
    uint32_t page;          /* page holding this record */
} record_info_t;

/** Initialise a store backed by two pages. Returns 0 on success. */
int record_store_init(record_store_t *store, const record_flash_if_t *flash,
                      uint32_t page_a, uint32_t page_b, uint16_t max_payload);

/** Load the newest valid record. Returns 0 on success, -1 when none exists. */
int record_store_load(const record_store_t *store, void *out, uint32_t out_capacity,
                      record_info_t *info);

/**
 * Write a record. @p new_sequence receives the sequence that was assigned.
 * Returns 0 on success.
 */
int record_store_save(record_store_t *store, const void *payload, uint32_t payload_len,
                      uint32_t *new_sequence);

/** Erase both pages (used by "factory reset" style operations). */
int record_store_clear(record_store_t *store);

#endif /* RECORD_STORE_H */
