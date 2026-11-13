// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    crash_handler.h
 * @brief   Cortex-M fault handlers and the retained-RAM hand-off.
 *
 * A fault is captured into a .noinit structure that survives the reset issued
 * at the end of the handler. The next boot calls crash_handler_pending() and
 * persists the record before clearing it.
 */

#ifndef CRASH_HANDLER_H
#define CRASH_HANDLER_H

#include "crash_dump.h"

#include <stdbool.h>

/** @return true when a fault record survived the last reset. */
bool crash_handler_pending(crash_record_t *out);

/** Forget the retained record (called after a successful flash append). */
void crash_handler_clear_pending(void);

#endif /* CRASH_HANDLER_H */
