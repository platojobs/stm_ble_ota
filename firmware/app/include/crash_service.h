// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    crash_service.h
 * @brief   Persistent crash history: retained-RAM hand-off and flash ring.
 *
 * crash_service_init() must run once during boot. It persists a record left in
 * retained RAM by the fault handler, then exposes the ring to the CLI and BLE.
 */

#ifndef CRASH_SERVICE_H
#define CRASH_SERVICE_H

#include "crash_dump.h"

#include <stdint.h>
#include <stdbool.h>

void crash_service_init(void);

uint16_t crash_service_count(void);
uint16_t crash_service_capacity(void);

/** Copy a stored record by age (0 = oldest). */
bool crash_service_get(uint16_t index, crash_record_t *out);

/** One JSON object with the count and the newest record ("last":null if none). */
uint32_t crash_service_last_json(char *out, uint32_t capacity);

/** Erase the whole ring. */
int crash_service_clear(void);

#endif /* CRASH_SERVICE_H */
