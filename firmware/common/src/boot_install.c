// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "boot_install.h"
#include "flash_layout.h"
#include "image_check.h"
#include "crc.h"

#include <string.h>

static image_flash_if_t make_image_if(const boot_install_io_t *io)
{
    image_flash_if_t image_if;
    image_if.context = io->context;
    image_if.read = io->flash_read;
    return image_if;
}

static bool slot_a_valid(const boot_install_io_t *io, const ota_metadata_t *meta)
{
    image_flash_if_t image_if = make_image_if(io);
    return image_validate(&image_if, FLASH_SLOT_A_START, FLASH_SLOT_A_SIZE,
                          meta->image_size, meta->image_crc32,
                          FLASH_SLOT_A_START) == IMAGE_OK;
}

static bool slot_b_valid(const boot_install_io_t *io, const ota_metadata_t *meta)
{
    image_flash_if_t image_if = make_image_if(io);
    /* The image is linked for slot A even while it sits in slot B. */
    return image_validate(&image_if, FLASH_SLOT_B_START, FLASH_SLOT_B_SIZE,
                          meta->image_size, meta->image_crc32,
                          FLASH_SLOT_A_START) == IMAGE_OK;
}

static bool slot_a_probe_ok(const boot_install_io_t *io)
{
    image_flash_if_t image_if = make_image_if(io);
    return image_probe(&image_if, FLASH_SLOT_A_START, FLASH_SLOT_A_SIZE) == IMAGE_OK;
}

static int save_state(const boot_install_io_t *io, ota_metadata_t *meta)
{
    return (io->metadata_save != NULL) ? io->metadata_save(io->context, meta) : -1;
}

/* Erase slot A pages covering image_size and copy slot B into slot A. */
static int copy_b_to_a(const boot_install_io_t *io, uint32_t image_size)
{
    uint32_t pages = (image_size + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE;
    uint8_t block[16];

    for (uint32_t page = 0; page < pages; ++page) {
        if (io->flash_erase_page(io->context, FLASH_SLOT_A_START + page * FLASH_PAGE_SIZE) != 0) {
            return -1;
        }
    }

    for (uint32_t offset = 0; offset < image_size; offset += 16u) {
        if (io->flash_read(io->context, FLASH_SLOT_B_START + offset, block, 16u) != 0) {
            return -1;
        }
        if (io->flash_program(io->context, FLASH_SLOT_A_START + offset, block, 16u) != 0) {
            return -1;
        }
    }
    return 0;
}

static int install_slot_b(const boot_install_io_t *io, ota_metadata_t *meta,
                          bool *installed)
{
    *installed = false;
    if (meta->install_attempts >= BOOT_MAX_INSTALL_ATTEMPTS) {
        meta->state = OTA_STATE_ERROR;
        return save_state(io, meta); /* keep B, report failure, caller decides */
    }

    meta->install_attempts++;
    meta->state = OTA_STATE_INSTALLING;
    if (save_state(io, meta) != 0) {
        return -1;
    }

    if (copy_b_to_a(io, meta->image_size) != 0) {
        return -1; /* stay INSTALLING so the next boot retries */
    }

    image_flash_if_t image_if = make_image_if(io);
    if (image_validate(&image_if, FLASH_SLOT_A_START, FLASH_SLOT_A_SIZE,
                       meta->image_size, meta->image_crc32,
                       FLASH_SLOT_A_START) != IMAGE_OK) {
        return -1;
    }

    meta->state = OTA_STATE_INSTALLED;
    meta->install_attempts = 0u;
    if (save_state(io, meta) != 0) {
        return -1;
    }
    *installed = true;
    return 0;
}

int boot_install_run(const boot_install_io_t *io, const record_store_t *meta_store,
                     boot_install_result_t *result)
{
    if (io == NULL || meta_store == NULL || result == NULL) {
        return -1;
    }

    memset(result, 0, sizeof(*result));
    result->decision = BOOT_DECISION_STAY;
    result->boot_address = FLASH_SLOT_A_START;
    result->reason = OTA_REASON_NONE;

    ota_metadata_t meta;
    bool found = false;
    ota_metadata_load_or_default(meta_store, &meta, &found);
    result->metadata_found = found;
    result->install_attempts = meta.install_attempts;

    if (!found) {
        /* Factory image flashed with a programmer: vector table is all we have. */
        if (slot_a_probe_ok(io)) {
            result->decision = BOOT_DECISION_BOOT_A;
        }
        return 0;
    }

    if ((ota_state_t)meta.state == OTA_STATE_READY_TO_BOOT) {
        if (meta.image_version < meta.previous_version) {
            meta.state = OTA_STATE_ERROR;
            (void)save_state(io, &meta);
            result->reason = OTA_REASON_BAD_VERSION;
            result->decision = slot_a_valid(io, &meta) ? BOOT_DECISION_BOOT_A
                                                       : BOOT_DECISION_STAY;
            result->install_attempts = meta.install_attempts;
            return 0;
        }
        if (!slot_b_valid(io, &meta)) {
            meta.state = OTA_STATE_ERROR;
            (void)save_state(io, &meta);
            result->reason = OTA_REASON_BAD_CRC;
            result->decision = slot_a_valid(io, &meta) ? BOOT_DECISION_BOOT_A
                                                       : BOOT_DECISION_STAY;
            result->install_attempts = meta.install_attempts;
            return 0;
        }
    }

    switch ((ota_state_t)meta.state) {
    case OTA_STATE_READY_TO_BOOT:
    case OTA_STATE_INSTALLING:
        if (!slot_b_valid(io, &meta)) {
            meta.state = OTA_STATE_ERROR;
            (void)save_state(io, &meta);
            result->reason = OTA_REASON_BAD_CRC;
            result->decision = slot_a_valid(io, &meta) ? BOOT_DECISION_BOOT_A
                                                       : BOOT_DECISION_STAY;
            break;
        }
        {
            bool installed = false;
            if (install_slot_b(io, &meta, &installed) == 0 && installed) {
                result->installed_now = true;
                result->decision = BOOT_DECISION_BOOT_INSTALLED;
            } else {
                result->reason = OTA_REASON_FLASH_ERROR;
                result->decision = slot_a_valid(io, &meta) ? BOOT_DECISION_BOOT_A
                                                           : BOOT_DECISION_STAY;
            }
        }
        break;

    case OTA_STATE_INSTALLED:
    case OTA_STATE_IDLE:
    case OTA_STATE_ERROR:
    default:
        if (slot_a_valid(io, &meta)) {
            result->decision = BOOT_DECISION_BOOT_A;
        } else if (slot_b_valid(io, &meta)) {
            bool installed = false;
            if (install_slot_b(io, &meta, &installed) == 0 && installed) {
                result->installed_now = true;
                result->decision = BOOT_DECISION_BOOT_INSTALLED;
            } else {
                result->reason = OTA_REASON_FLASH_ERROR;
                result->decision = BOOT_DECISION_STAY;
            }
        } else if (slot_a_probe_ok(io)) {
            result->decision = BOOT_DECISION_BOOT_A;
        } else {
            result->decision = BOOT_DECISION_STAY;
        }
        break;
    }

    result->install_attempts = meta.install_attempts;
    return 0;
}
