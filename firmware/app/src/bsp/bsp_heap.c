// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    bsp_heap.c
 * @brief   Bounded _sbrk() with current/peak accounting.
 *
 * The heap window comes from the linker script (.heap), so an accidental
 * allocation cannot grow into the stack. The firmware does not allocate today;
 * this keeps newlib printf/malloc safe and makes any future use observable.
 */

#include "bsp_heap.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

extern uint8_t _heap_start[];
extern uint8_t _heap_end[];

static uint8_t *g_break;
static uint32_t g_peak;

static void ensure_break(void)
{
    if (g_break == NULL) {
        g_break = _heap_start;
    }
}

void bsp_heap_get_stats(bsp_heap_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    ensure_break();
    out->total = (uint32_t)((uintptr_t)_heap_end - (uintptr_t)_heap_start);
    out->used = (uint32_t)((uintptr_t)g_break - (uintptr_t)_heap_start);
    out->peak = g_peak;
}

void *_sbrk(ptrdiff_t increment)
{
    ensure_break();

    uintptr_t base = (uintptr_t)_heap_start;
    uintptr_t limit = (uintptr_t)_heap_end;
    intptr_t next = (intptr_t)(uintptr_t)g_break + (intptr_t)increment;
    if (next < (intptr_t)base) {
        errno = ENOMEM;
        return (void *)-1;
    }

    uintptr_t aligned = ((uintptr_t)next + 7u) & ~(uintptr_t)7u;
    if (aligned > limit) {
        errno = ENOMEM;
        return (void *)-1;
    }

    uint32_t used = (uint32_t)(aligned - base);
    if (used > g_peak) {
        g_peak = used;
    }

    void *previous = g_break;
    g_break = (uint8_t *)aligned;
    return previous;
}
