// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    crash_dump.c
 * @brief   CRC-sealed exception records and their JSON representation.
 */

#include "crash_dump.h"
#include "crc.h"

#include <stdio.h>
#include <string.h>

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(crash_record_t) <= 256u,
               "crash_record_t must fit in one crash-log slot");
#endif

void crash_record_init(crash_record_t *record)
{
    if (record == NULL) {
        return;
    }
    memset(record, 0, sizeof(*record));
    record->magic = CRASH_RECORD_MAGIC;
    record->record_version = (uint16_t)CRASH_RECORD_VERSION;
    record->record_size = (uint16_t)sizeof(*record);
}

static uint32_t record_crc(const crash_record_t *record)
{
    return crc32_ieee_buf(record, sizeof(*record) - sizeof(record->crc32));
}

void crash_record_seal(crash_record_t *record)
{
    if (record == NULL) {
        return;
    }
    record->crc32 = record_crc(record);
}

bool crash_record_valid(const crash_record_t *record)
{
    if (record == NULL) {
        return false;
    }
    if (record->magic != CRASH_RECORD_MAGIC ||
        record->record_version != (uint16_t)CRASH_RECORD_VERSION ||
        record->record_size != (uint16_t)sizeof(*record)) {
        return false;
    }
    return record->crc32 == record_crc(record);
}

void crash_capture(crash_record_t *record, crash_exception_t exception,
                   const uint32_t frame[8], const crash_sys_regs_t *regs,
                   uint32_t frame_address, crash_mem_read_t read_mem,
                   void *context)
{
    if (record == NULL || frame == NULL) {
        return;
    }

    crash_record_init(record);
    record->exception = (uint32_t)exception;

    if (regs != NULL) {
        record->cfsr = regs->cfsr;
        record->hfsr = regs->hfsr;
        record->mmfar = regs->mmfar;
        record->bfar = regs->bfar;
    }

    record->r0 = frame[0];
    record->r1 = frame[1];
    record->r2 = frame[2];
    record->r3 = frame[3];
    record->r12 = frame[4];
    record->lr = frame[5];
    record->pc = frame[6];
    record->xpsr = frame[7];
    record->sp = frame_address;

    /* Prefer the precise fault address; fall back to the faulting instruction. */
    if (regs != NULL && (regs->cfsr & (1u << 7)) != 0u) {         /* MMARVALID */
        record->fault_address = regs->mmfar;
    } else if (regs != NULL && (regs->cfsr & (1u << 15)) != 0u) { /* BFARVALID */
        record->fault_address = regs->bfar;
    } else {
        record->fault_address = record->pc;
    }

    /* Summary of the caller's stack, just above the stacked exception frame. */
    uint32_t sample = frame_address + 8u * sizeof(uint32_t);
    record->stack_pointer = sample;
    for (uint32_t i = 0; i < CRASH_STACK_WORDS; ++i) {
        uint32_t value = 0u;
        if (read_mem != NULL &&
            !read_mem(context, sample + i * sizeof(uint32_t), &value)) {
            record->stack_pointer = 0u;
            value = 0u;
        }
        record->stack_words[i] = value;
    }

    crash_record_seal(record);
}

const char *crash_exception_name(crash_exception_t exception)
{
    switch (exception) {
    case CRASH_EXCEPTION_HARD_FAULT: return "HARDFAULT";
    case CRASH_EXCEPTION_MEMMANAGE:  return "MEMMANAGE";
    case CRASH_EXCEPTION_BUSFAULT:   return "BUSFAULT";
    case CRASH_EXCEPTION_USAGEFAULT: return "USAGEFAULT";
    case CRASH_EXCEPTION_NONE:
    default:                         return "NONE";
    }
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

uint32_t crash_record_to_json(const crash_record_t *record, char *out,
                              uint32_t capacity)
{
    if (record == NULL || out == NULL || capacity < 2u) {
        return 0u;
    }

    uint32_t used = 0u;
    APPEND("{\"seq\":%u,\"exception\":%u,\"name\":\"%s\","
           "\"pc\":%u,\"lr\":%u,\"sp\":%u,\"addr\":%u,"
           "\"cfsr\":%u,\"hfsr\":%u,\"mmfar\":%u,\"bfar\":%u,"
           "\"r0\":%u,\"r1\":%u,\"r2\":%u,\"r3\":%u,\"r12\":%u,\"xpsr\":%u,"
           "\"stack\":[",
           (unsigned)record->sequence, (unsigned)record->exception,
           crash_exception_name((crash_exception_t)record->exception),
           (unsigned)record->pc, (unsigned)record->lr, (unsigned)record->sp,
           (unsigned)record->fault_address, (unsigned)record->cfsr,
           (unsigned)record->hfsr, (unsigned)record->mmfar,
           (unsigned)record->bfar, (unsigned)record->r0,
           (unsigned)record->r1, (unsigned)record->r2, (unsigned)record->r3,
           (unsigned)record->r12, (unsigned)record->xpsr);

    for (uint32_t i = 0; i < CRASH_STACK_WORDS; ++i) {
        APPEND("%s%u", (i == 0u) ? "" : ",", (unsigned)record->stack_words[i]);
    }

    APPEND("],\"stack_ptr\":%u,\"fw\":\"%u.%u.%u\",\"fw_raw\":%u,"
           "\"build_id\":%u,\"hw\":%u,\"uptime_ms\":%u}",
           (unsigned)record->stack_pointer,
           (unsigned)((record->firmware_version >> 16) & 0xFFu),
           (unsigned)((record->firmware_version >> 8) & 0xFFu),
           (unsigned)(record->firmware_version & 0xFFu),
           (unsigned)record->firmware_version, (unsigned)record->build_id,
           (unsigned)record->hardware_id, (unsigned)record->uptime_ms);
    return used;
}
