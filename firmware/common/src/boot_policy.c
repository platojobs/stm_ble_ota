// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "boot_policy.h"

boot_action_t boot_policy_evaluate(uint32_t attempts,
                                   bool backup_valid,
                                   bool slot_a_valid)
{
    if (!slot_a_valid) {
        /* Without a runnable image there is nothing to boot. A valid backup is
         * the only way out; otherwise the bootloader holds for recovery. */
        return backup_valid ? BOOT_ACTION_ROLLBACK : BOOT_ACTION_HOLD;
    }

    if (attempts >= BOOT_MAX_UNCONFIRMED_BOOTS && backup_valid) {
        /* The current image has failed to confirm often enough: fall back. */
        return BOOT_ACTION_ROLLBACK;
    }

    return BOOT_ACTION_BOOT_CURRENT;
}
