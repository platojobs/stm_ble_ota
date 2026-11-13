// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    bsp_heap.h
 * @brief   newlib heap accounting through a custom _sbrk().
 */

#ifndef BSP_HEAP_H
#define BSP_HEAP_H

#include <stdint.h>

typedef struct {
    uint32_t total; /* bytes between _heap_start and _heap_end */
    uint32_t used;  /* current break offset from _heap_start      */
    uint32_t peak;  /* highest used value since boot              */
} bsp_heap_stats_t;

void bsp_heap_get_stats(bsp_heap_stats_t *out);

#endif /* BSP_HEAP_H */
