// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    stack_guard.c
 * @brief   Stack paint and canary implementation.
 */

#include "stack_guard.h"

#include <stddef.h>

bool stack_guard_arm(stack_guard_t *guard, uintptr_t stack_low,
                     uintptr_t stack_high, uintptr_t current_sp)
{
    if (guard == NULL) {
        return false;
    }
    guard->armed = false;
    guard->low = stack_low;
    guard->high = stack_high;

    if (stack_high <= stack_low || (stack_high - stack_low) < STACK_GUARD_MIN_SIZE) {
        return false;
    }

    uintptr_t paint_high = stack_high;
    if (current_sp > stack_low + STACK_GUARD_SP_MARGIN) {
        paint_high = current_sp - STACK_GUARD_SP_MARGIN;
    }
    if (paint_high > stack_high) {
        paint_high = stack_high;
    }

    /* The canary occupies the lowest word; the paint starts right above it. */
    guard->canary = STACK_GUARD_CANARY;
    *(volatile uint32_t *)(stack_low) = STACK_GUARD_CANARY;

    guard->paint_low = stack_low + sizeof(uint32_t);
    guard->paint_high = paint_high;
    for (uintptr_t address = guard->paint_low;
         address + sizeof(uint32_t) <= guard->paint_high;
         address += sizeof(uint32_t)) {
        *(volatile uint32_t *)address = STACK_GUARD_PATTERN;
    }

    guard->armed = true;
    return true;
}

uint32_t stack_guard_used(const stack_guard_t *guard)
{
    if (guard == NULL || !guard->armed) {
        return 0u;
    }
    if (guard->high <= guard->low) {
        return 0u;
    }

    uintptr_t first_used = guard->paint_high;
    for (uintptr_t address = guard->paint_low;
         address + sizeof(uint32_t) <= guard->paint_high;
         address += sizeof(uint32_t)) {
        if (*(const volatile uint32_t *)address != STACK_GUARD_PATTERN) {
            first_used = address;
            break;
        }
    }

    uintptr_t used = guard->high - first_used;
    if (used > (guard->high - guard->low)) {
        used = guard->high - guard->low;
    }
    return (uint32_t)used;
}

uint32_t stack_guard_free(const stack_guard_t *guard)
{
    if (guard == NULL || !guard->armed) {
        return 0u;
    }
    uint32_t used = stack_guard_used(guard);
    uint32_t total = (uint32_t)(guard->high - guard->low);
    return (used >= total) ? 0u : (total - used);
}

bool stack_guard_canary_ok(const stack_guard_t *guard)
{
    if (guard == NULL || !guard->armed) {
        return true;
    }
    return *(const volatile uint32_t *)(guard->low) == STACK_GUARD_CANARY;
}
