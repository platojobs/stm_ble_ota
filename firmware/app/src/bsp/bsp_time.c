/**
 * @file    bsp_time.c
 * @brief   SysTick 1 ms time base.
 */

#include "bsp_time.h"
#include "stm32c562xx.h"

static volatile uint32_t g_ms;

void SysTick_Handler(void)
{
    ++g_ms;
}

void bsp_time_init(void)
{
    (void)SysTick_Config(SystemCoreClock / 1000u);
    NVIC_SetPriority(SysTick_IRQn, 1u);
}

uint32_t bsp_time_ms(void)
{
    return g_ms;
}

void bsp_delay_ms(uint32_t ms)
{
    uint32_t start = g_ms;
    while ((g_ms - start) < ms) {
        __WFI();
    }
}
