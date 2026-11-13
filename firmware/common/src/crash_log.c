// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    crash_log.c
 * @brief   Flash ring implementation for the persistent crash records.
 */

#include "crash_log.h"
#include "flash_layout.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t slot;
    uint32_t sequence;
} slot_ref_t;

static uint32_t slot_address(const crash_log_t *log, uint16_t slot)
{
    return log->base_page + (uint32_t)slot * FLASH_PAGE_SIZE;
}

static bool read_slot(const crash_log_t *log, uint16_t slot, crash_record_t *out)
{
    if (log->flash->read(log->flash->context, slot_address(log, slot), out,
                         (uint32_t)sizeof(*out)) != 0) {
        return false;
    }
    return crash_record_valid(out);
}

/* Collect valid slots sorted by ascending sequence (oldest first). */
static uint16_t collect_slots(const crash_log_t *log,
                              slot_ref_t refs[CRASH_LOG_MAX_SLOTS])
{
    uint16_t count = 0u;
    for (uint16_t slot = 0; slot < log->slot_count; ++slot) {
        crash_record_t record;
        if (!read_slot(log, slot, &record)) {
            continue;
        }

        uint16_t insert = count;
        while (insert > 0u && refs[insert - 1u].sequence > record.sequence) {
            refs[insert] = refs[insert - 1u];
            --insert;
        }
        refs[insert].slot = slot;
        refs[insert].sequence = record.sequence;
        ++count;
    }
    return count;
}

int crash_log_init(crash_log_t *log, const record_flash_if_t *flash,
                   uint32_t base_page, uint16_t slot_count)
{
    if (log == NULL || flash == NULL || flash->erase_page == NULL ||
        flash->program == NULL || flash->read == NULL) {
        return -1;
    }
    if (slot_count == 0u || slot_count > CRASH_LOG_MAX_SLOTS) {
        return -1;
    }
    memset(log, 0, sizeof(*log));
    log->flash = flash;
    log->base_page = base_page;
    log->slot_count = slot_count;
    return 0;
}

uint16_t crash_log_count(const crash_log_t *log)
{
    if (log == NULL) {
        return 0u;
    }
    slot_ref_t refs[CRASH_LOG_MAX_SLOTS];
    return collect_slots(log, refs);
}

bool crash_log_get(const crash_log_t *log, uint16_t index, crash_record_t *out)
{
    if (log == NULL || out == NULL) {
        return false;
    }
    slot_ref_t refs[CRASH_LOG_MAX_SLOTS];
    uint16_t count = collect_slots(log, refs);
    if (index >= count) {
        return false;
    }
    return read_slot(log, refs[index].slot, out);
}

int crash_log_append(crash_log_t *log, crash_record_t *record)
{
    if (log == NULL || record == NULL) {
        return -1;
    }

    slot_ref_t refs[CRASH_LOG_MAX_SLOTS];
    uint16_t count = collect_slots(log, refs);

    uint16_t target;
    uint32_t sequence;
    if (count > 0u) {
        target = (uint16_t)((refs[count - 1u].slot + 1u) % log->slot_count);
        sequence = refs[count - 1u].sequence + 1u;
        if (sequence == 0u) {
            sequence = 1u; /* wrap; the CRC check makes duplicates harmless */
        }
    } else {
        target = 0u;
        sequence = 1u;
    }

    record->magic = CRASH_RECORD_MAGIC;
    record->record_version = (uint16_t)CRASH_RECORD_VERSION;
    record->record_size = (uint16_t)sizeof(*record);
    record->sequence = sequence;
    crash_record_seal(record);

    uint32_t page = slot_address(log, target);
    if (log->flash->erase_page(log->flash->context, page) != 0) {
        return -1;
    }

    /* Stream in 16-byte granules, padding the tail with the erased value. */
    uint8_t granule[16];
    uint32_t offset = 0u;
    const uint8_t *bytes = (const uint8_t *)record;
    while (offset < sizeof(*record)) {
        uint32_t remaining = (uint32_t)sizeof(*record) - offset;
        uint32_t n = (remaining > sizeof(granule)) ? (uint32_t)sizeof(granule)
                                                   : remaining;
        if (n < sizeof(granule)) {
            memset(granule, 0xFF, sizeof(granule));
        }
        memcpy(granule, bytes + offset, n);
        if (log->flash->program(log->flash->context, page + offset, granule,
                                sizeof(granule)) != 0) {
            return -1;
        }
        offset += n;
    }

    /* Read back so a silent program failure is detected in the same boot. */
    crash_record_t check;
    if (log->flash->read(log->flash->context, page, &check, sizeof(check)) != 0 ||
        !crash_record_valid(&check) || check.sequence != sequence) {
        return -1;
    }
    return 0;
}

int crash_log_clear(crash_log_t *log)
{
    if (log == NULL) {
        return -1;
    }
    for (uint16_t slot = 0; slot < log->slot_count; ++slot) {
        if (log->flash->erase_page(log->flash->context,
                                   slot_address(log, slot)) != 0) {
            return -1;
        }
    }
    return 0;
}

#define APPEND(...)                                                          \
    do {                                                                     \
        uint32_t remaining = capacity - used;                                \
        int written = snprintf(out + used, remaining, __VA_ARGS__);           \
        if (written < 0 || (uint32_t)written >= remaining) {                 \
            out[0] = '\0';                                                   \
            return 0u;                                                       \
        }                                                                    \
        used += (uint32_t)written;                                           \
    } while (0)

uint32_t crash_log_to_json(const crash_log_t *log, char *out, uint32_t capacity)
{
    if (log == NULL || out == NULL || capacity < 2u) {
        return 0u;
    }

    slot_ref_t refs[CRASH_LOG_MAX_SLOTS];
    uint16_t count = collect_slots(log, refs);

    uint32_t used = 0u;
    APPEND("{\"type\":\"crash\",\"count\":%u,\"capacity\":%u,\"records\":[",
           (unsigned)count, (unsigned)log->slot_count);

    char record_json[768];
    uint16_t written_records = 0u;
    for (uint16_t i = 0; i < count; ++i) {
        crash_record_t record;
        if (!read_slot(log, refs[i].slot, &record)) {
            continue;
        }
        if (crash_record_to_json(&record, record_json, sizeof(record_json)) == 0u) {
            out[0] = '\0';
            return 0u;
        }
        APPEND("%s%s", (written_records == 0u) ? "" : ",", record_json);
        ++written_records;
    }

    APPEND("]}");
    return used;
}
