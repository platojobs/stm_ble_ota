// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    crash_service.c
 * @brief   Wires the crash ring to the internal flash and the retained record.
 */

#include "crash_service.h"
#include "crash_handler.h"
#include "crash_log.h"
#include "bsp_flash.h"
#include "flash_layout.h"
#include "fault_service.h"
#include "err_code.h"

#include <stdio.h>
#include <string.h>

static crash_log_t g_log;
static record_flash_if_t g_iface;
static bool g_ready;

static int crash_erase(void *context, uint32_t address)
{
    (void)context;
    return bsp_flash_erase_page(address);
}

static int crash_program(void *context, uint32_t address, const void *data,
                         uint32_t length)
{
    (void)context;
    return bsp_flash_program(address, data, length);
}

static int crash_read(void *context, uint32_t address, void *out, uint32_t length)
{
    (void)context;
    return bsp_flash_read(address, out, length);
}

void crash_service_init(void)
{
    g_ready = false;
    g_iface.context = NULL;
    g_iface.erase_page = crash_erase;
    g_iface.program = crash_program;
    g_iface.read = crash_read;

    if (crash_log_init(&g_log, &g_iface, FLASH_CRASH_LOG_START,
                       (uint16_t)FLASH_CRASH_LOG_CAPACITY) != 0) {
        return;
    }
    g_ready = true;

    crash_record_t pending;
    if (crash_handler_pending(&pending)) {
        if (crash_log_append(&g_log, &pending) == 0) {
            crash_handler_clear_pending();
        }
        fault_service_raise(ERR_CODE_CRASH_DETECTED, pending.pc);
    }
}

uint16_t crash_service_count(void)
{
    return g_ready ? crash_log_count(&g_log) : 0u;
}

uint16_t crash_service_capacity(void)
{
    return g_ready ? g_log.slot_count : 0u;
}

bool crash_service_get(uint16_t index, crash_record_t *out)
{
    return g_ready && crash_log_get(&g_log, index, out);
}

uint32_t crash_service_last_json(char *out, uint32_t capacity)
{
    if (out == NULL || capacity < 2u) {
        return 0u;
    }
    if (!g_ready) {
        (void)snprintf(out, capacity, "{\"type\":\"crash\",\"available\":false}");
        return (uint32_t)strlen(out);
    }

    uint16_t count = crash_log_count(&g_log);
    if (count == 0u) {
        int written = snprintf(out, capacity,
                               "{\"type\":\"crash\",\"count\":0,\"capacity\":%u,"
                               "\"last\":null}",
                               (unsigned)g_log.slot_count);
        return (written > 0 && (uint32_t)written < capacity) ? (uint32_t)written
                                                             : 0u;
    }

    crash_record_t record;
    if (!crash_log_get(&g_log, (uint16_t)(count - 1u), &record)) {
        return 0u;
    }
    char record_json[768];
    if (crash_record_to_json(&record, record_json, sizeof(record_json)) == 0u) {
        return 0u;
    }

    int written = snprintf(out, capacity,
                           "{\"type\":\"crash\",\"count\":%u,\"capacity\":%u,"
                           "\"last\":%s}",
                           (unsigned)count, (unsigned)g_log.slot_count,
                           record_json);
    return (written > 0 && (uint32_t)written < capacity) ? (uint32_t)written : 0u;
}

int crash_service_clear(void)
{
    if (!g_ready) {
        return -1;
    }
    return crash_log_clear(&g_log);
}
