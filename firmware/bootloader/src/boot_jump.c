// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    boot_jump.c
 * @brief   Jump from the bootloader into the application in slot A.
 */

#include "boot_jump.h"
#include "stm32c562xx.h"

void boot_jump_to_app(uint32_t image_base)
{
    uint32_t stack_pointer = *(volatile uint32_t *)(uintptr_t)image_base;
    uint32_t reset_handler = *(volatile uint32_t *)(uintptr_t)(image_base + 4u);

    /* Stop anything the bootloader may still have running. */
    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;

    __disable_irq();

    SCB->VTOR = image_base;
    __DSB();
    __ISB();

    /* Re-point the main stack and hand over to the application reset code. */
    __set_MSP(stack_pointer);

    void (*entry)(void) = (void (*)(void))(uintptr_t)reset_handler;
    entry();

    /* Not reachable; keep the compiler from assuming a return path. */
    for (;;) {
    }
}
