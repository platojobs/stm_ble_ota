// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    stack_guard.h
 * @brief   Stack high-water measurement and overflow canary.
 *
 * The region is painted with a known pattern below the current stack pointer
 * and a canary sits at its lowest address. Scanning the paint at runtime gives
 * the deepest stack usage since arming; a damaged canary means the stack grew
 * below the configured limit. The region is described with uintptr_t so the
 * same code runs on the target and in host tests.
 */

#ifndef STACK_GUARD_H
#define STACK_GUARD_H

#include <stdint.h>
#include <stdbool.h>

#define STACK_GUARD_PATTERN     0xC5A5C5A5u
#define STACK_GUARD_CANARY      0xDEADBEEFu
#define STACK_GUARD_MIN_SIZE    256u
#define STACK_GUARD_SP_MARGIN   64u

typedef struct {
    uintptr_t low;
    uintptr_t high;
    uintptr_t paint_low;
    uintptr_t paint_high;
    uint32_t canary;
    bool armed;
} stack_guard_t;

/**
 * Paint the unused part of [stack_low, stack_high) and place the canary.
 * @param current_sp stack pointer at the call site; memory below
 *                   current_sp - STACK_GUARD_SP_MARGIN is painted.
 * @return false when the region is too small to monitor.
 */
bool stack_guard_arm(stack_guard_t *guard, uintptr_t stack_low,
                     uintptr_t stack_high, uintptr_t current_sp);

/** Deepest stack usage since arming, in bytes. */
uint32_t stack_guard_used(const stack_guard_t *guard);

/** Bytes never touched since arming (high - used), clamped at zero. */
uint32_t stack_guard_free(const stack_guard_t *guard);

/** @return true when the canary still holds its initial value. */
bool stack_guard_canary_ok(const stack_guard_t *guard);

#endif /* STACK_GUARD_H */
