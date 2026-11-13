// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    crash_handler.c
 * @brief   HardFault/MemManage/BusFault/UsageFault capture for the app.
 *
 * The exception entry code selects MSP or PSP from EXC_RETURN bit 2 and passes
 * the stacked frame to crash_fault_handler_c(). The record is written to a
 * .noinit structure (see app.ld) and the MCU is reset; the reset preserves SRAM,
 * so crash_service can persist the record on the next boot. Flash is never
 * touched from the fault handler.
 */

#include "crash_handler.h"
#include "app_config.h"
#include "bsp_time.h"
#include "flash_layout.h"
#include "stm32c562xx.h"

#include <stddef.h>
#include <stdint.h>

#define CRASH_RETAINED_MAGIC 0x43524352u /* "CRCR" */

typedef struct {
    uint32_t magic;
    crash_record_t record;
} crash_retained_t;

/* Not zeroed by the CMSIS startup code: .noinit is outside .bss. */
static crash_retained_t g_retained __attribute__((section(".noinit")));

static bool target_read(void *context, uint32_t address, uint32_t *out)
{
    (void)context;
    if (out == NULL || (address & 3u) != 0u) {
        return false;
    }
    if (address < SRAM_START || address > (SRAM_END - sizeof(uint32_t))) {
        return false;
    }
    *out = *(const volatile uint32_t *)(uintptr_t)address;
    return true;
}

void crash_fault_handler_c(uint32_t *frame, uint32_t exc_return, uint32_t type)
{
    (void)exc_return;

    crash_sys_regs_t regs;
    regs.cfsr = SCB->CFSR;
    regs.hfsr = SCB->HFSR;
    regs.mmfar = SCB->MMFAR;
    regs.bfar = SCB->BFAR;

    crash_record_t record;
    crash_capture(&record, (crash_exception_t)type, frame, &regs,
                  (uint32_t)(uintptr_t)frame, target_read, NULL);
    record.firmware_version = APP_VERSION;
    record.build_id = APP_BUILD_ID;
    record.hardware_id = APP_HARDWARE_ID;
    record.uptime_ms = bsp_time_ms();
    crash_record_seal(&record);

    g_retained.record = record;
    __DSB();
    g_retained.magic = CRASH_RETAINED_MAGIC;
    __DSB();
    __ISB();

    NVIC_SystemReset();
    for (;;) {
    }
}

#define CRASH_HANDLER(name, type_value)                                       \
    __attribute__((naked)) void name(void)                                    \
    {                                                                         \
        __asm volatile(                                                       \
            "tst lr, #4\n\t"                                                  \
            "ite eq\n\t"                                                      \
            "mrseq r0, msp\n\t"                                               \
            "mrsne r0, psp\n\t"                                               \
            "mov r1, lr\n\t"                                                  \
            "movs r2, #" #type_value "\n\t"                                   \
            "ldr r3, =crash_fault_handler_c\n\t"                              \
            "bx r3\n\t");                                                     \
    }

CRASH_HANDLER(HardFault_Handler, 1)
CRASH_HANDLER(MemManage_Handler, 2)
CRASH_HANDLER(BusFault_Handler, 3)
CRASH_HANDLER(UsageFault_Handler, 4)

bool crash_handler_pending(crash_record_t *out)
{
    if (out == NULL || g_retained.magic != CRASH_RETAINED_MAGIC) {
        return false;
    }
    if (!crash_record_valid(&g_retained.record)) {
        return false;
    }
    *out = g_retained.record;
    return true;
}

void crash_handler_clear_pending(void)
{
    g_retained.magic = 0u;
}
