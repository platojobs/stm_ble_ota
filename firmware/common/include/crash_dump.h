// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    crash_dump.h
 * @brief   Portable description of a CPU exception captured on Cortex-M.
 *
 * The platform-neutral part lives here so the decoder, the CRC and the JSON
 * rendering can be unit tested on a host. The target-specific part — reading
 * SCB and the exception frame in the fault handler — is in the application's
 * crash_handler.c.
 *
 * A captured record is first stored in retained RAM (the .noinit section in the
 * application linker script) so it survives the reset that terminates the fault.
 * On the next boot crash_service persists it into the flash ring in bank 2.
 */

#ifndef CRASH_DUMP_H
#define CRASH_DUMP_H

#include <stdint.h>
#include <stdbool.h>

#define CRASH_RECORD_MAGIC   0x43524331u /* "CRC1" */
#define CRASH_RECORD_VERSION 1u
#define CRASH_STACK_WORDS    8u

typedef enum {
    CRASH_EXCEPTION_NONE = 0,
    CRASH_EXCEPTION_HARD_FAULT = 1,
    CRASH_EXCEPTION_MEMMANAGE = 2,
    CRASH_EXCEPTION_BUSFAULT = 3,
    CRASH_EXCEPTION_USAGEFAULT = 4,
} crash_exception_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;           /* CRASH_RECORD_MAGIC                           */
    uint16_t record_version;  /* CRASH_RECORD_VERSION                         */
    uint16_t record_size;     /* sizeof(crash_record_t)                       */
    uint32_t sequence;        /* assignment order in the flash ring           */
    uint32_t exception;       /* crash_exception_t                            */
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t sp;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
    uint32_t fault_address;   /* MMFAR/BFAR when valid, otherwise PC          */
    uint32_t stack_pointer;   /* address of stack_words[0], 0 when unreadable */
    uint32_t stack_words[CRASH_STACK_WORDS];
    uint32_t firmware_version;/* 0x00MMmmpp                                   */
    uint32_t build_id;
    uint32_t hardware_id;
    uint32_t uptime_ms;
    uint32_t crc32;           /* CRC-32 of everything above                   */
} crash_record_t;

typedef struct {
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
} crash_sys_regs_t;

/** Reads one aligned 32-bit word. Returns false when the address is invalid. */
typedef bool (*crash_mem_read_t)(void *context, uint32_t address, uint32_t *out);

/** Fill a record with the fixed magic/version/size fields. */
void crash_record_init(crash_record_t *record);

/** Recompute and store crc32. Call after modifying any other field. */
void crash_record_seal(crash_record_t *record);

/** @return true when magic, version, size, sequence and CRC are all sane. */
bool crash_record_valid(const crash_record_t *record);

/**
 * Decode an exception frame.
 *
 * @param frame         eight words as stacked by the Cortex-M exception entry:
 *                      R0, R1, R2, R3, R12, LR, PC, xPSR
 * @param frame_address address of frame[0]; stack_words[] is sampled just
 *                      above the exception frame through @p read_mem
 */
void crash_capture(crash_record_t *record, crash_exception_t exception,
                   const uint32_t frame[8], const crash_sys_regs_t *regs,
                   uint32_t frame_address, crash_mem_read_t read_mem,
                   void *context);

const char *crash_exception_name(crash_exception_t exception);

/** Standalone JSON object: {"type":"crash",...}. Returns bytes or 0. */
uint32_t crash_record_to_json(const crash_record_t *record, char *out,
                              uint32_t capacity);

#endif /* CRASH_DUMP_H */
