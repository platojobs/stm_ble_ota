// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    resource_monitor.h
 * @brief   Runtime stack/heap/RAM budget monitoring.
 *
 * Stack: the unused part is painted at boot; the report shows the deepest use
 * since then and the state of the canary at the stack limit. Heap: accounted by
 * the bounded _sbrk() in bsp_heap.c, including the peak. RAM: the linker
 * reservation is compared with the documented application budget.
 */

#ifndef RESOURCE_MONITOR_H
#define RESOURCE_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/* Application RAM budget (flash budget is enforced by tools/check_size.py).
 * The measured reservation includes .data/.bss/.noinit plus the configured
 * heap and stack. Below the margin the boot check raises ERR_CODE_RAM_LOW. */
#define RESOURCE_RAM_BUDGET      24576u /* 24 KB */
#define RESOURCE_RAM_MIN_MARGIN  2048u  /* 2 KB  */

typedef struct {
    uint32_t stack_total;
    uint32_t stack_used;
    uint32_t stack_free;
    bool stack_armed;
    bool stack_canary_ok;

    uint32_t heap_total;
    uint32_t heap_used;
    uint32_t heap_peak;

    uint32_t ram_reserved;
    uint32_t ram_budget;
    uint32_t ram_margin;
    bool ram_ok;
} resource_stats_t;

/** Paint the stack, arm the canary and run the boot RAM check. */
void resource_monitor_init(void);

/** Re-check the canary; raises STACK_OVERFLOW once when it is damaged. */
void resource_monitor_poll(void);

void resource_monitor_get(resource_stats_t *out);

/** JSON object for the CLI. Returns bytes or 0 when the buffer is too small. */
uint32_t resource_monitor_json(char *out, uint32_t capacity);

#endif /* RESOURCE_MONITOR_H */
