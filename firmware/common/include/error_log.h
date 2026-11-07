// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    error_log.h
 * @brief   Small, power-of-ten-free ring buffer of recent device faults.
 *
 * The log stores the last ERROR_LOG_CAPACITY non-zero err_code_t values
 * together with a millisecond timestamp and a 32-bit detail word. It lives in
 * RAM only: it is evidence for the current boot, not persistent history. The
 * bootloader and the application each own their own instance.
 *
 * The buffer overwrites the oldest entry when full and tracks how many entries
 * have been dropped, so the CLI can tell "no faults" from "too many faults".
 * The module is platform independent; the caller supplies the timestamp.
 */

#ifndef ERROR_LOG_H
#define ERROR_LOG_H

#include "err_code.h"

#include <stdint.h>
#include <stdbool.h>

#define ERROR_LOG_CAPACITY 8u

typedef struct {
    uint32_t timestamp_ms;
    uint32_t detail;
    uint16_t code;      /* err_code_t */
    uint16_t reserved;
} error_log_entry_t;

typedef struct {
    error_log_entry_t entries[ERROR_LOG_CAPACITY];
    uint16_t head;      /* next slot to write */
    uint16_t count;     /* valid entries, 0..ERROR_LOG_CAPACITY */
    uint16_t dropped;   /* entries overwritten since init */
    uint16_t reserved;
    uint32_t total;     /* total entries pushed since init */
} error_log_t;

void error_log_init(error_log_t *log);

/** Append a fault. ERR_CODE_OK is ignored. @return false when ignored/rejected. */
bool error_log_push(error_log_t *log, err_code_t code, uint32_t detail,
                    uint32_t now_ms);

uint16_t error_log_count(const error_log_t *log);
uint16_t error_log_dropped(const error_log_t *log);
uint32_t error_log_total(const error_log_t *log);

/** Most recent fault code, or ERR_CODE_OK when the log is empty. */
err_code_t error_log_last_code(const error_log_t *log);

/**
 * Copy an entry by age: 0 = oldest retained, count-1 = newest.
 * @return false when the index is out of range.
 */
bool error_log_get(const error_log_t *log, uint16_t index, error_log_entry_t *out);

/**
 * Render the whole log as one JSON object:
 * {"type":"fault","count":N,"total":N,"dropped":N,"entries":[...]}
 *
 * @return bytes written excluding the NUL, or 0 when the buffer is too small.
 */
uint32_t error_log_to_json(const error_log_t *log, char *out, uint32_t capacity);

#endif /* ERROR_LOG_H */
