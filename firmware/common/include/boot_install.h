// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    boot_install.h
 * @brief   Boot-time decision logic shared by the bootloader and host tests.
 *
 * The bootloader never erases slot A until the downloaded image in slot B has
 * been fully validated (size, vector table, CRC). This guarantees that a
 * corrupt download leaves the old application bootable. If power is lost while
 * copying, slot B is still intact and the copy is repeated on the next boot.
 */

#ifndef BOOT_INSTALL_H
#define BOOT_INSTALL_H

#include "ota_metadata.h"
#include "record_store.h"

#include <stdint.h>
#include <stdbool.h>

#define BOOT_MAX_INSTALL_ATTEMPTS 3u

typedef struct {
    void *context;
    int (*flash_erase_page)(void *context, uint32_t page_address);
    int (*flash_program)(void *context, uint32_t address, const void *data,
                         uint32_t length);
    int (*flash_read)(void *context, uint32_t address, void *out, uint32_t length);
    int (*metadata_save)(void *context, const ota_metadata_t *metadata);
} boot_install_io_t;

typedef enum {
    BOOT_DECISION_STAY = 0,     /* no valid application, stay in bootloader     */
    BOOT_DECISION_BOOT_A = 1,   /* jump to slot A                               */
    BOOT_DECISION_BOOT_INSTALLED = 2, /* slot A was just installed, jump to it  */
} boot_decision_t;

typedef struct {
    boot_decision_t decision;
    ota_reason_t reason;
    bool metadata_found;
    bool installed_now;
    uint32_t install_attempts;
    uint32_t boot_address;
} boot_install_result_t;

/**
 * Run the boot decision state machine and, when required, install slot B into
 * slot A. The caller only has to honour @c result.decision.
 */
int boot_install_run(const boot_install_io_t *io, const record_store_t *meta_store,
                     boot_install_result_t *result);

#endif /* BOOT_INSTALL_H */
