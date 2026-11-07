// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    boot_policy.h
 * @brief   Pure, host-testable boot/rollback decision logic.
 *
 * The bootloader records how many consecutive boots reached the bootloader
 * without the application confirming a successful start (by clearing the
 * attempt counter). Three unconfirmed boots roll the device back to the
 * previously known-good image when one is available; otherwise the bootloader
 * stops and waits for recovery.
 *
 * Keeping the arithmetic here (instead of spread across the flash state
 * machine) makes the rollback threshold and the "stay in bootloader" rule
 * directly unit-testable.
 */

#ifndef BOOT_POLICY_H
#define BOOT_POLICY_H

#include <stdint.h>
#include <stdbool.h>

#define BOOT_MAX_UNCONFIRMED_BOOTS 3u

typedef enum {
    BOOT_ACTION_BOOT_CURRENT = 0, /* start the image in slot A normally       */
    BOOT_ACTION_ROLLBACK = 1,     /* restore BACKUP into slot A, then boot it  */
    BOOT_ACTION_HOLD = 2,         /* stay in the bootloader for recovery       */
} boot_action_t;

/**
 * Decide what the bootloader should do before jumping to slot A.
 *
 * @param attempts       consecutive boots without APP confirmation (from
 *                       metadata, before this boot is counted).
 * @param backup_valid   true when a verified backup image exists.
 * @param slot_a_valid   true when slot A is itself a valid image. A device
 *                       with an invalid slot A and no backup cannot boot.
 * @param reset_to_factory_rollback  (for future use; currently ignored)
 */
boot_action_t boot_policy_evaluate(uint32_t attempts,
                                   bool backup_valid,
                                   bool slot_a_valid);

#endif /* BOOT_POLICY_H */
