// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "test_framework.h"
#include "stack_guard.h"

#include <string.h>

static uint32_t g_stack[128]; /* 512 bytes */

TEST_CASE(stack_guard_arms_and_measures_high_water)
{
    memset(g_stack, 0, sizeof(g_stack));
    stack_guard_t guard;

    uintptr_t low = (uintptr_t)g_stack;
    uintptr_t high = low + sizeof(g_stack);
    ASSERT_TRUE(stack_guard_arm(&guard, low, high, high - 32u));
    ASSERT_TRUE(guard.armed);

    /* The canary occupies the lowest word, everything below the current SP
     * (minus the guard margin) is painted. */
    ASSERT_EQ(g_stack[0], STACK_GUARD_CANARY);
    ASSERT_EQ(g_stack[1], STACK_GUARD_PATTERN);
    ASSERT_EQ(g_stack[127], 0u); /* above SP, untouched */
    ASSERT_TRUE(stack_guard_canary_ok(&guard));

    /* SP was 32 bytes below the top; the paint stops 64 bytes below that. */
    ASSERT_EQ(stack_guard_used(&guard), 96u);

    /* Simulate a call 128 bytes deep. */
    *(volatile uint32_t *)(high - 128u) = 0x12345678u;
    ASSERT_EQ(stack_guard_used(&guard), 128u);
    ASSERT_EQ(stack_guard_free(&guard), (uint32_t)sizeof(g_stack) - 128u);
    ASSERT_TRUE(stack_guard_canary_ok(&guard));
}

TEST_CASE(stack_guard_detects_canary_damage)
{
    memset(g_stack, 0, sizeof(g_stack));
    stack_guard_t guard;
    uintptr_t low = (uintptr_t)g_stack;
    uintptr_t high = low + sizeof(g_stack);

    ASSERT_TRUE(stack_guard_arm(&guard, low, high, high));
    ASSERT_TRUE(stack_guard_canary_ok(&guard));

    g_stack[0] = 0u; /* stack grew past the configured limit */
    ASSERT_FALSE(stack_guard_canary_ok(&guard));
}

TEST_CASE(stack_guard_rejects_small_regions)
{
    uint32_t tiny[16];
    stack_guard_t guard;
    uintptr_t low = (uintptr_t)tiny;
    uintptr_t high = low + sizeof(tiny);

    ASSERT_FALSE(stack_guard_arm(&guard, low, high, high));
    ASSERT_FALSE(guard.armed);
    ASSERT_EQ(stack_guard_used(&guard), 0u);
    ASSERT_EQ(stack_guard_free(&guard), 0u);
    ASSERT_TRUE(stack_guard_canary_ok(&guard));
    ASSERT_FALSE(stack_guard_arm(NULL, low, high, high));
}

void run_stack_guard_tests(void)
{
    RUN_TEST(stack_guard_arms_and_measures_high_water);
    RUN_TEST(stack_guard_detects_canary_damage);
    RUN_TEST(stack_guard_rejects_small_regions);
}
