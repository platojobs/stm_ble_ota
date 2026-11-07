// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    error_log.c
 * @brief   Ring buffer implementation and JSON rendering for fault reports.
 */

#include "error_log.h"

#include <stdio.h>
#include <string.h>

void error_log_init(error_log_t *log)
{
    if (log == NULL) {
        return;
    }
    memset(log, 0, sizeof(*log));
}

bool error_log_push(error_log_t *log, err_code_t code, uint32_t detail,
                    uint32_t now_ms)
{
    if (log == NULL || code == ERR_CODE_OK) {
        return false;
    }

    error_log_entry_t *slot = &log->entries[log->head];
    slot->timestamp_ms = now_ms;
    slot->detail = detail;
    slot->code = (uint16_t)code;
    slot->reserved = 0u;

    log->head = (uint16_t)((log->head + 1u) % ERROR_LOG_CAPACITY);
    if (log->count < ERROR_LOG_CAPACITY) {
        log->count++;
    } else {
        log->dropped++;
    }
    log->total++;
    return true;
}

uint16_t error_log_count(const error_log_t *log)
{
    return (log != NULL) ? log->count : 0u;
}

uint16_t error_log_dropped(const error_log_t *log)
{
    return (log != NULL) ? log->dropped : 0u;
}

uint32_t error_log_total(const error_log_t *log)
{
    return (log != NULL) ? log->total : 0u;
}

err_code_t error_log_last_code(const error_log_t *log)
{
    if (log == NULL || log->count == 0u) {
        return ERR_CODE_OK;
    }
    uint16_t last = (uint16_t)((log->head + ERROR_LOG_CAPACITY - 1u) %
                               ERROR_LOG_CAPACITY);
    return (err_code_t)log->entries[last].code;
}

bool error_log_get(const error_log_t *log, uint16_t index, error_log_entry_t *out)
{
    if (log == NULL || out == NULL || index >= log->count) {
        return false;
    }

    uint16_t oldest = (uint16_t)((log->head + ERROR_LOG_CAPACITY - log->count) %
                                 ERROR_LOG_CAPACITY);
    uint16_t slot = (uint16_t)((oldest + index) % ERROR_LOG_CAPACITY);
    *out = log->entries[slot];
    return true;
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

uint32_t error_log_to_json(const error_log_t *log, char *out, uint32_t capacity)
{
    if (log == NULL || out == NULL || capacity < 2u) {
        return 0u;
    }

    uint32_t used = 0u;
    APPEND("{\"type\":\"fault\",\"count\":%u,\"total\":%u,\"dropped\":%u,"
           "\"entries\":[",
           (unsigned)log->count, (unsigned)log->total, (unsigned)log->dropped);

    for (uint16_t i = 0; i < log->count; ++i) {
        error_log_entry_t entry;
        (void)error_log_get(log, i, &entry);
        err_code_t code = (err_code_t)entry.code;
        APPEND("%s{\"t_ms\":%u,\"code\":%u,\"name\":\"%s\",\"detail\":%u}",
               (i == 0u) ? "" : ",", (unsigned)entry.timestamp_ms,
               (unsigned)entry.code, err_code_name(code),
               (unsigned)entry.detail);
    }

    APPEND("]}");
    return used;
}
