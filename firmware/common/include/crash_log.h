// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    crash_log.h
 * @brief   Power-loss-safe ring of the last N exception records.
 *
 * Each slot occupies one entire 8 KB flash page. Slots are written in sequence;
 * when the ring is full the oldest page is erased and reused. A torn erase or
 * program therefore only damages the oldest record, and the loader ignores any
 * slot whose magic, size, version or CRC is wrong. The flash interface is
 * injected so the logic runs in host unit tests.
 */

#ifndef CRASH_LOG_H
#define CRASH_LOG_H

#include "record_store.h"
#include "crash_dump.h"

#include <stdint.h>
#include <stdbool.h>

#define CRASH_LOG_MAX_SLOTS 16u

typedef struct {
    const record_flash_if_t *flash;
    uint32_t base_page;
    uint16_t slot_count;
} crash_log_t;

/** Initialise the ring. @return 0 on success. */
int crash_log_init(crash_log_t *log, const record_flash_if_t *flash,
                   uint32_t base_page, uint16_t slot_count);

/** Number of valid records currently stored (0..slot_count). */
uint16_t crash_log_count(const crash_log_t *log);

/**
 * Read a record by age: 0 = oldest retained, count-1 = newest.
 * @return false when the index is out of range or the slot is unreadable.
 */
bool crash_log_get(const crash_log_t *log, uint16_t index, crash_record_t *out);

/**
 * Append a record. The sequence number, magic, version, size and CRC fields are
 * filled in by the ring; everything else is preserved.
 * @return 0 on success.
 */
int crash_log_append(crash_log_t *log, crash_record_t *record);

/** Erase every slot. Existing records are gone afterwards. */
int crash_log_clear(crash_log_t *log);

/**
 * Render the whole ring as one JSON object:
 * {"type":"crash","count":N,"capacity":N,"records":[...]}
 *
 * @return bytes written excluding the NUL, or 0 when the buffer is too small.
 */
uint32_t crash_log_to_json(const crash_log_t *log, char *out, uint32_t capacity);

#endif /* CRASH_LOG_H */
