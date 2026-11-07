// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "boot_install.h"
#include "boot_policy.h"
#include "flash_layout.h"
#include "image_check.h"
#include "crc.h"
#include "sha256.h"
#include "ed25519_verify.h"
#include "ota_protocol.h"

#include <string.h>

static image_flash_if_t make_image_if(const boot_install_io_t *io)
{
    image_flash_if_t image_if;
    image_if.context = io->context;
    image_if.read = io->flash_read;
    return image_if;
}

static bool all_zero(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) {
        if (p[i] != 0u) {
            return false;
        }
    }
    return true;
}

static int save_state(const boot_install_io_t *io, ota_metadata_t *meta)
{
    return (io->metadata_save != NULL) ? io->metadata_save(io->context, meta) : -1;
}

static bool validate_slot(const boot_install_io_t *io, uint32_t base,
                          uint32_t slot_size, uint32_t image_size,
                          uint32_t crc, const uint8_t *sha256,
                          const uint8_t *signature,
                          uint32_t version, uint32_t build_id,
                          uint16_t hardware_id, ota_reason_t *fail_reason)
{
    image_flash_if_t image_if = make_image_if(io);

    if (image_size == 0u || image_size > slot_size) {
        if (fail_reason) *fail_reason = OTA_REASON_BAD_SIZE;
        return false;
    }

    image_expectation_t expect;
    ota_signature_blob_t blob; /* must outlive the if-block below */
    expect.image_size = image_size;
    expect.expected_crc = crc;
    expect.link_base = FLASH_SLOT_A_START;
    expect.sha256 = (sha256 != NULL && !all_zero(sha256, 32u)) ? sha256 : NULL;
    if (signature != NULL && !all_zero(signature, 64u) &&
        expect.sha256 != NULL) {
        ota_signature_blob_build(&blob, sha256, image_size, version,
                                 build_id, hardware_id);
        expect.signed_blob = &blob;
        expect.signature = signature;
    } else {
        expect.signed_blob = NULL;
        expect.signature = NULL;
    }

    image_check_result_t r = image_validate_full(&image_if, base, slot_size,
                                                 &expect);
    if (r != IMAGE_OK) {
        if (fail_reason) {
            switch (r) {
            case IMAGE_ERR_SIGNATURE: *fail_reason = OTA_REASON_BAD_SIGNATURE; break;
            case IMAGE_ERR_SHA256:    *fail_reason = OTA_REASON_BAD_CRC;       break;
            case IMAGE_ERR_CRC:       *fail_reason = OTA_REASON_BAD_CRC;       break;
            default:                  *fail_reason = OTA_REASON_FLASH_ERROR;   break;
            }
        }
        return false;
    }
    return true;
}

static bool slot_a_valid_with_running(const boot_install_io_t *io,
                                      const ota_metadata_t *meta,
                                      ota_reason_t *fail_reason)
{
    /* The running image's signature was verified when it was installed.
     * Re-verify integrity (CRC + SHA-256 + vectors) but skip the signature,
     * whose blob embeds the original build_id/hardware_id that are not
     * retained in the running descriptor. */
    return validate_slot(io, FLASH_SLOT_A_START, FLASH_SLOT_A_SIZE,
                         meta->running_size, meta->running_crc32,
                         meta->running_sha256, NULL,
                         meta->running_version, 0u, 0u, fail_reason);
}

static bool slot_a_valid_with_image(const boot_install_io_t *io,
                                    const ota_metadata_t *meta,
                                    ota_reason_t *fail_reason)
{
    return validate_slot(io, FLASH_SLOT_A_START, FLASH_SLOT_A_SIZE,
                         meta->image_size, meta->image_crc32,
                         meta->image_sha256, meta->image_signature,
                         meta->image_version, meta->build_id,
                         (uint16_t)meta->hardware_id, fail_reason);
}

static bool slot_b_valid(const boot_install_io_t *io, const ota_metadata_t *meta,
                         ota_reason_t *fail_reason)
{
    return validate_slot(io, FLASH_SLOT_B_START, FLASH_SLOT_B_SIZE,
                         meta->image_size, meta->image_crc32,
                         meta->image_sha256, meta->image_signature,
                         meta->image_version, meta->build_id,
                         (uint16_t)meta->hardware_id, fail_reason);
}

static bool slot_a_probe_ok(const boot_install_io_t *io)
{
    image_flash_if_t image_if = make_image_if(io);
    return image_probe(&image_if, FLASH_SLOT_A_START, FLASH_SLOT_A_SIZE) == IMAGE_OK;
}

static bool backup_probe_ok(const boot_install_io_t *io)
{
    image_flash_if_t image_if = make_image_if(io);
    return image_probe(&image_if, FLASH_BACKUP_START, FLASH_BACKUP_SIZE) == IMAGE_OK;
}

static int copy_region(const boot_install_io_t *io, uint32_t dst, uint32_t src,
                       uint32_t size)
{
    uint8_t block[16];
    for (uint32_t offset = 0; offset < size; offset += 16u) {
        if (io->flash_read(io->context, src + offset, block, 16u) != 0) {
            return -1;
        }
        if (io->flash_program(io->context, dst + offset, block, 16u) != 0) {
            return -1;
        }
    }
    return 0;
}

static int erase_region_pages(const boot_install_io_t *io, uint32_t start,
                              uint32_t size)
{
    uint32_t pages = (size + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE;
    for (uint32_t page = 0; page < pages; ++page) {
        if (io->flash_erase_page(io->context, start + page * FLASH_PAGE_SIZE) != 0) {
            return -1;
        }
    }
    return 0;
}

static int crc_of_region(const boot_install_io_t *io, uint32_t base,
                         uint32_t size, uint32_t *crc_out,
                         uint8_t sha_out[32])
{
    uint32_t crc = 0xFFFFFFFFu;
    sha256_t sha;
    uint8_t chunk[64];
    uint32_t offset = 0u;

    sha256_init(&sha);
    while (offset < size) {
        uint32_t n = size - offset;
        if (n > sizeof(chunk)) {
            n = (uint32_t)sizeof(chunk);
        }
        if (io->flash_read(io->context, base + offset, chunk, n) != 0) {
            return -1;
        }
        crc = crc32_ieee(chunk, n, crc);
        sha256_update(&sha, chunk, n);
        offset += n;
    }
    if (crc_out != NULL) {
        *crc_out = crc ^ 0xFFFFFFFFu;
    }
    if (sha_out != NULL) {
        sha256_final(&sha, sha_out);
    }
    return 0;
}

/* Copy running slot A into BACKUP and, after reading it back, mark it valid. */
static int snapshot_backup(const boot_install_io_t *io, ota_metadata_t *meta)
{
    uint32_t size = (meta->running_size != 0u &&
                     meta->running_size <= FLASH_SLOT_A_SIZE)
                        ? meta->running_size
                        : FLASH_SLOT_A_SIZE;

    if (erase_region_pages(io, FLASH_BACKUP_START, size) != 0) {
        return -1;
    }
    if (copy_region(io, FLASH_BACKUP_START, FLASH_SLOT_A_START, size) != 0) {
        return -1;
    }

    uint32_t crc;
    uint8_t sha[32];
    if (crc_of_region(io, FLASH_BACKUP_START, size, &crc, sha) != 0) {
        return -1;
    }
    /* Re-read the first vector to confirm the copy is a runnable image. */
    if (!backup_probe_ok(io)) {
        return -1;
    }

    meta->backup_valid = 1u;
    meta->backup_size = size;
    meta->backup_crc32 = crc;
    meta->backup_version = meta->running_version;
    memcpy(meta->backup_sha256, sha, sizeof(meta->backup_sha256));
    if (meta->running_size != 0u) {
        memcpy(meta->backup_signature, meta->running_signature,
               sizeof(meta->backup_signature));
    }
    return save_state(io, meta);
}

static int restore_backup(const boot_install_io_t *io, ota_metadata_t *meta)
{
    if (meta->backup_size == 0u || meta->backup_size > FLASH_SLOT_A_SIZE) {
        return -1;
    }
    if (erase_region_pages(io, FLASH_SLOT_A_START, meta->backup_size) != 0) {
        return -1;
    }
    if (copy_region(io, FLASH_SLOT_A_START, FLASH_BACKUP_START,
                    meta->backup_size) != 0) {
        return -1;
    }
    ota_reason_t fr;
    if (!validate_slot(io, FLASH_SLOT_A_START, FLASH_SLOT_A_SIZE,
                       meta->backup_size, meta->backup_crc32,
                       meta->backup_sha256, NULL,
                       meta->backup_version, 0u, 0u, &fr)) {
        return -1;
    }

    /* The restored image becomes the new "running" / installed image. */
    meta->image_size = meta->backup_size;
    meta->image_crc32 = meta->backup_crc32;
    meta->image_version = meta->backup_version;
    memcpy(meta->image_sha256, meta->backup_sha256,
           sizeof(meta->image_sha256));
    memcpy(meta->image_signature, meta->backup_signature,
           sizeof(meta->image_signature));
    ota_metadata_promote_running(meta);

    meta->backup_valid = 0u;
    meta->backup_size = 0u;
    meta->state = OTA_STATE_INSTALLED;
    meta->install_attempts = 0u;
    return save_state(io, meta);
}

static int install_slot_b(const boot_install_io_t *io, ota_metadata_t *meta,
                          bool *installed)
{
    *installed = false;
    if (meta->install_attempts >= BOOT_MAX_INSTALL_ATTEMPTS) {
        meta->state = OTA_STATE_ERROR;
        return save_state(io, meta);
    }

    meta->install_attempts++;
    meta->state = OTA_STATE_INSTALLING;
    if (save_state(io, meta) != 0) {
        return -1;
    }

    if (erase_region_pages(io, FLASH_SLOT_A_START, meta->image_size) != 0) {
        return -1;
    }
    if (copy_region(io, FLASH_SLOT_A_START, FLASH_SLOT_B_START,
                    meta->image_size) != 0) {
        return -1;
    }

    ota_reason_t fr;
    if (!slot_a_valid_with_image(io, meta, &fr)) {
        return -1; /* stay INSTALLING; next boot retries from intact slot B */
    }

    ota_metadata_promote_running(meta);
    meta->state = OTA_STATE_INSTALLED;
    meta->install_attempts = 0u;
    if (save_state(io, meta) != 0) {
        return -1;
    }
    *installed = true;
    return 0;
}

static bool has_running_descriptor(const ota_metadata_t *meta)
{
    return meta->running_size != 0u;
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
        if (slot_a_probe_ok(io)) {
            result->decision = BOOT_DECISION_BOOT_A;
        }
        return 0;
    }

    /* Factory / first OTA: derive running descriptor from the installed image
     * so the backup snapshot carries correct size/crc/sha. */
    if (!has_running_descriptor(&meta) &&
        (ota_state_t)meta.state == OTA_STATE_INSTALLED && meta.image_size != 0u) {
        ota_metadata_promote_running(&meta);
        (void)save_state(io, &meta);
    }

    /* ---- State: READY_TO_BOOT / INSTALLING ----------------------------- */
    if ((ota_state_t)meta.state == OTA_STATE_READY_TO_BOOT ||
        (ota_state_t)meta.state == OTA_STATE_INSTALLING) {

        uint32_t current_version = (meta.running_version != 0u)
                                       ? meta.running_version
                                       : meta.previous_version;
        if (current_version != 0u && meta.image_version < current_version) {
            /* Defence in depth: the receiver already refuses downgrades, but a
             * tampered metadata record must not install an older image. */
            meta.state = OTA_STATE_ERROR;
            (void)save_state(io, &meta);
            result->reason = OTA_REASON_BAD_VERSION;
            result->decision = (has_running_descriptor(&meta) &&
                                slot_a_valid_with_running(io, &meta, NULL))
                                   ? BOOT_DECISION_BOOT_A
                                   : (slot_a_probe_ok(io) ? BOOT_DECISION_BOOT_A
                                                          : BOOT_DECISION_STAY);
            return 0;
        }

        ota_reason_t fail = OTA_REASON_NONE;
        if (!slot_b_valid(io, &meta, &fail)) {
            meta.state = OTA_STATE_ERROR;
            (void)save_state(io, &meta);
            result->reason = fail;
            if (has_running_descriptor(&meta)) {
                result->decision = slot_a_valid_with_running(io, &meta, NULL)
                                       ? BOOT_DECISION_BOOT_A
                                       : BOOT_DECISION_STAY;
            } else {
                result->decision = slot_a_probe_ok(io) ? BOOT_DECISION_BOOT_A
                                                        : BOOT_DECISION_STAY;
            }
            return 0;
        }

        /* On the first attempt, snapshot slot A into BACKUP before any write
         * to slot A. backup_valid is only set after the copy is verified. */
        if ((ota_state_t)meta.state == OTA_STATE_READY_TO_BOOT) {
            meta.previous_version = meta.running_version;
            if (has_running_descriptor(&meta) || slot_a_probe_ok(io)) {
                if (snapshot_backup(io, &meta) != 0) {
                    result->reason = OTA_REASON_FLASH_ERROR;
                    result->decision = has_running_descriptor(&meta)
                                           ? BOOT_DECISION_BOOT_A
                                           : BOOT_DECISION_STAY;
                    return 0;
                }
            }
        }

        bool installed = false;
        if (install_slot_b(io, &meta, &installed) == 0 && installed) {
            result->installed_now = true;
            result->decision = BOOT_DECISION_BOOT_INSTALLED;
        } else {
            result->reason = OTA_REASON_FLASH_ERROR;
            result->decision = has_running_descriptor(&meta)
                                   ? BOOT_DECISION_BOOT_A
                                   : BOOT_DECISION_STAY;
        }
        result->install_attempts = meta.install_attempts;
        return 0;
    }

    /* ---- State: IDLE / INSTALLED / ERROR / RECEIVING ------------------ */
    ota_reason_t a_fail = OTA_REASON_NONE;
    bool a_ok = has_running_descriptor(&meta)
                    ? slot_a_valid_with_running(io, &meta, &a_fail)
                    : slot_a_probe_ok(io);
    boot_action_t action = boot_policy_evaluate(meta.boot_attempts,
                                                meta.backup_valid != 0u, a_ok);

    switch (action) {
    case BOOT_ACTION_ROLLBACK:
        if (restore_backup(io, &meta) == 0) {
            meta.boot_attempts = 0u;
            (void)save_state(io, &meta);
            result->rolled_back = true;
            result->decision = BOOT_DECISION_ROLLED_BACK;
        } else {
            /* Backup restore failed: try booting A anyway if it is valid,
             * otherwise hold. */
            result->reason = OTA_REASON_FLASH_ERROR;
            result->decision = a_ok ? BOOT_DECISION_BOOT_A : BOOT_DECISION_STAY;
        }
        break;

    case BOOT_ACTION_HOLD:
        result->decision = BOOT_DECISION_STAY;
        result->reason = OTA_REASON_FLASH_ERROR;
        break;

    case BOOT_ACTION_BOOT_CURRENT:
    default:
        /* The bootloader will increment boot_attempts before the jump; the
         * application clears it on a successful start (confirmation). */
        result->decision = a_ok ? BOOT_DECISION_BOOT_A : BOOT_DECISION_STAY;
        break;
    }

    result->install_attempts = meta.boot_attempts;
    return 0;
}
