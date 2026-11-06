/**
 * @file    bsp_time.h
 * @brief   1 ms system tick used for timeouts and periodic scheduling.
 */

#ifndef BSP_TIME_H
#define BSP_TIME_H

#include <stdint.h>

/** Configure SysTick for a 1 ms interrupt. Must be called once at boot. */
void bsp_time_init(void);

/** Milliseconds since boot (wraps after ~49.7 days). */
uint32_t bsp_time_ms(void);

/** Busy-wait delay using the same tick source. */
void bsp_delay_ms(uint32_t ms);

#endif /* BSP_TIME_H */
