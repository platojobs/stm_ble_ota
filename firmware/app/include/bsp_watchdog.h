// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    bsp_watchdog.h
 * @brief   Independent watchdog (IWDG) control for the application.
 *
 * The bootloader starts the IWDG before jumping into the application. The
 * application must reload it on every main loop iteration (feed) and clear the
 * boot_attempts counter once initialisation succeeds. Failure to reload within
 * the ~2 second window resets the device; repeated unconfirmed boots roll the
 * bootloader back to the previous image.
 */

#ifndef BSP_WATCHDOG_H
#define BSP_WATCHDOG_H

#include <stdint.h>

/** Start the IWDG with the bootloader's 2 s window. Idempotent. */
void bsp_watchdog_init(void);

/** Reload the watchdog counter. Call at least once per 2 s. */
void bsp_watchdog_feed(void);

#endif /* BSP_WATCHDOG_H */
