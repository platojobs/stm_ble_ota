// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "bsp_watchdog.h"
#include "stm32c562xx.h"

void bsp_watchdog_init(void)
{
    IWDG->KR = 0x5555u; /* unlock PR/RLR */
    IWDG->PR = 6u;      /* /64            */
    IWDG->RLR = 999u;   /* ~2.0 s         */
    while ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0u) {
    }
    IWDG->KR = 0xAAAAu; /* reload         */
    IWDG->KR = 0xCCCCu; /* start if off   */
}

void bsp_watchdog_feed(void)
{
    IWDG->KR = 0xAAAAu;
}
