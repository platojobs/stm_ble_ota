// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    fault_service.h
 * @brief   Application-wide fault ring buffer used by the CLI and BLE info.
 *
 * Services call fault_service_raise() when they detect a failure. The log is
 * RAM-only and survives until the next reset, which makes it useful to inspect
 * what went wrong without a debugger.
 */

#ifndef FAULT_SERVICE_H
#define FAULT_SERVICE_H

#include "err_code.h"
#include "error_log.h"

#include <stdint.h>

void fault_service_init(void);

/** Record a fault with a detail word (e.g. received bytes or driver status). */
void fault_service_raise(err_code_t code, uint32_t detail);

uint16_t fault_service_count(void);
err_code_t fault_service_last_code(void);

/** JSON form of the whole log; returns bytes written or 0 when too small. */
uint32_t fault_service_json(char *out, uint32_t capacity);

#endif /* FAULT_SERVICE_H */
