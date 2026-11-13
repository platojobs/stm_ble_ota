// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    resource_monitor.c
 * @brief   Stack paint/canary, heap stats and the boot RAM margin check.
 */

#include "resource_monitor.h"
#include "bsp_heap.h"
#include "flash_layout.h"
#include "fault_service.h"
#include "err_code.h"
#include "stack_guard.h"
#include "stm32c562xx.h"

#include <stddef.h>
#include <stdio.h>

extern uint32_t __StackLimit;
extern uint32_t __StackTop;

static stack_guard_t g_stack;
static bool g_canary_reported;
static bool g_ram_ok = true;

static uint32_t ram_reserved(void)
{
    uintptr_t top = (uintptr_t)&__StackTop;
    uintptr_t base = (uintptr_t)SRAM_START;
    return (top > base) ? (uint32_t)(top - base) : 0u;
}

void resource_monitor_init(void)
{
    uintptr_t low = (uintptr_t)&__StackLimit;
    uintptr_t high = (uintptr_t)&__StackTop;
    (void)stack_guard_arm(&g_stack, low, high, (uintptr_t)__get_MSP());
    g_canary_reported = false;

    uint32_t reserved = ram_reserved();
    g_ram_ok = (reserved + RESOURCE_RAM_MIN_MARGIN) <= RESOURCE_RAM_BUDGET;
    if (!g_ram_ok) {
        fault_service_raise(ERR_CODE_RAM_LOW, reserved);
    }
}

void resource_monitor_poll(void)
{
    if (stack_guard_canary_ok(&g_stack)) {
        return;
    }
    if (!g_canary_reported) {
        g_canary_reported = true;
        fault_service_raise(ERR_CODE_STACK_OVERFLOW, stack_guard_used(&g_stack));
    }
}

void resource_monitor_get(resource_stats_t *out)
{
    if (out == NULL) {
        return;
    }

    bsp_heap_stats_t heap;
    bsp_heap_get_stats(&heap);

    out->stack_total = (uint32_t)(g_stack.high - g_stack.low);
    out->stack_used = stack_guard_used(&g_stack);
    out->stack_free = stack_guard_free(&g_stack);
    out->stack_armed = g_stack.armed;
    out->stack_canary_ok = stack_guard_canary_ok(&g_stack);

    out->heap_total = heap.total;
    out->heap_used = heap.used;
    out->heap_peak = heap.peak;

    out->ram_reserved = ram_reserved();
    out->ram_budget = RESOURCE_RAM_BUDGET;
    out->ram_margin = (out->ram_reserved < RESOURCE_RAM_BUDGET)
                          ? (RESOURCE_RAM_BUDGET - out->ram_reserved)
                          : 0u;
    out->ram_ok = g_ram_ok;
}

uint32_t resource_monitor_json(char *out, uint32_t capacity)
{
    if (out == NULL || capacity < 2u) {
        return 0u;
    }

    resource_stats_t stats;
    resource_monitor_get(&stats);

    int written = snprintf(
        out, capacity,
        "{\"type\":\"resource\","
        "\"stack\":{\"total\":%u,\"used\":%u,\"free\":%u,"
        "\"armed\":%s,\"canary_ok\":%s},"
        "\"heap\":{\"total\":%u,\"used\":%u,\"peak\":%u},"
        "\"ram\":{\"budget\":%u,\"reserved\":%u,\"margin\":%u,\"ok\":%s}}",
        (unsigned)stats.stack_total, (unsigned)stats.stack_used,
        (unsigned)stats.stack_free, stats.stack_armed ? "true" : "false",
        stats.stack_canary_ok ? "true" : "false", (unsigned)stats.heap_total,
        (unsigned)stats.heap_used, (unsigned)stats.heap_peak,
        (unsigned)stats.ram_budget, (unsigned)stats.ram_reserved,
        (unsigned)stats.ram_margin, stats.ram_ok ? "true" : "false");

    return (written > 0 && (uint32_t)written < capacity) ? (uint32_t)written : 0u;
}
