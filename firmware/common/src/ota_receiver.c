// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "ota_receiver.h"
#include "flash_layout.h"
#include "crc.h"
#include "sha256.h"

#include <string.h>

#define OTA_RECEIVE_TIMEOUT_MS  10000u
#define OTA_PAGE_COUNT          (FLASH_SLOT_B_SIZE / FLASH_PAGE_SIZE)

_Static_assert(OTA_RESUME_MAX_CHUNKS <= 32u, "chunk bitmask must fit uint32_t");

static uint32_t chunk_length(const ota_receiver_t *receiver, uint32_t index)
{
    uint32_t start = index * OTA_RESUME_CHUNK_SIZE;
    if (start >= receiver->image_size) {
        return 0u;
    }
    uint32_t remaining = receiver->image_size - start;
    return (remaining > OTA_RESUME_CHUNK_SIZE) ? OTA_RESUME_CHUNK_SIZE : remaining;
}

static void report(ota_receiver_t *receiver, ota_state_t state,
                   ota_reason_t reason)
{
    receiver->state = state;
    receiver->last_reason = reason;
    if (receiver->io->notify != NULL) {
        receiver->io->notify(receiver->io->context, state, reason, receiver->received,
                             receiver->image_size, receiver->running_crc);
    }
}

static int save_meta(ota_receiver_t *receiver, ota_state_t state,
                     bool include_partial)
{
    if (receiver->io->metadata_save == NULL) {
        return 0;
    }

    ota_metadata_t meta;
    if (receiver->has_base_meta) {
        meta = receiver->base_meta;
    } else {
        ota_metadata_default(&meta);
    }

    meta.state = (uint32_t)state;
    meta.image_size = receiver->image_size;
    meta.image_crc32 = receiver->image_crc32;
    meta.image_version = receiver->image_version;
    meta.build_id = receiver->build_id;
    meta.hardware_id = receiver->hardware_id;
    meta.previous_version = receiver->previous_version;
    meta.chunk_size = OTA_RESUME_CHUNK_SIZE;
    memcpy(meta.image_sha256, receiver->image_sha256,
           sizeof(meta.image_sha256));
    memcpy(meta.image_signature, receiver->image_signature,
           sizeof(meta.image_signature));

    if (include_partial) {
        meta.received_bytes = receiver->received;
        meta.partial_crc32 = receiver->running_crc;
        meta.chunk_count = receiver->chunk_count;
        meta.chunk_valid = receiver->chunk_valid;
        memcpy(meta.chunk_crc32, receiver->chunk_crcs,
               sizeof(meta.chunk_crc32));
    } else {
        meta.received_bytes = 0u;
        meta.partial_crc32 = 0xFFFFFFFFu;
        meta.chunk_count = 0u;
        meta.chunk_valid = 0u;
        memset(meta.chunk_crc32, 0, sizeof(meta.chunk_crc32));
    }

    if (state == OTA_STATE_READY_TO_BOOT || state == OTA_STATE_INSTALLED) {
        meta.install_attempts = 0u;
    }

    int rc = receiver->io->metadata_save(receiver->io->context, &meta);
    if (rc == 0 && state == OTA_STATE_READY_TO_BOOT) {
        receiver->base_meta = meta;
        receiver->has_base_meta = true;
    }
    return rc;
}

static int ensure_page_erased(ota_receiver_t *receiver, uint32_t offset)
{
    uint32_t page = offset / FLASH_PAGE_SIZE;
    if (page >= OTA_PAGE_COUNT) {
        return -1;
    }
    uint32_t mask = 1u << page;
    if ((receiver->erased_pages & mask) != 0u) {
        return 0;
    }
    if (receiver->io->flash_erase_page(receiver->io->context,
                                       receiver->target_base +
                                           page * FLASH_PAGE_SIZE) != 0) {
        return -1;
    }
    receiver->erased_pages |= mask;
    return 0;
}

static int flush_stage(ota_receiver_t *receiver)
{
    if (receiver->stage_len == 0u) {
        return 0;
    }
    if (ensure_page_erased(receiver, receiver->written) != 0) {
        return -1;
    }
    /* Pad the final granule with 0xFF: ECC flash programs 16 bytes at a time. */
    if (receiver->stage_len < 16u) {
        memset(receiver->stage + receiver->stage_len, 0xFF,
               16u - receiver->stage_len);
    }
    if (receiver->io->flash_program(receiver->io->context,
                                    receiver->target_base + receiver->written,
                                    receiver->stage, 16u) != 0) {
        return -1;
    }
    receiver->written += 16u;
    receiver->stage_len = 0u;
    return 0;
}

static void mark_prefix_erased(ota_receiver_t *receiver)
{
    uint32_t pages = receiver->received / FLASH_PAGE_SIZE;
    if (pages > OTA_PAGE_COUNT) {
        pages = OTA_PAGE_COUNT;
    }
    receiver->erased_pages = (pages >= 32u) ? 0xFFFFFFFFu
                                            : ((1u << pages) - 1u);
}

static void reset_transfer(ota_receiver_t *receiver)
{
    receiver->received = 0u;
    receiver->written = 0u;
    receiver->running_crc = 0xFFFFFFFFu;
    receiver->erased_pages = 0u;
    receiver->last_progress = 0u;
    receiver->stage_len = 0u;
    receiver->chunk_count = 0u;
    receiver->chunk_index = 0u;
    receiver->chunk_crc32 = 0xFFFFFFFFu;
    receiver->chunk_valid = 0u;
    memset(receiver->chunk_crcs, 0, sizeof(receiver->chunk_crcs));
    receiver->partial_active = 1u;
    receiver->resumable = true;
}

/*
 * Re-read the persisted prefix from flash and keep only contiguous chunks whose
 * CRC matches the recorded value. This is what makes resume trustworthy after
 * a reset: metadata alone is not proof that the bytes are still there.
 */
static void verify_persisted_prefix(ota_receiver_t *receiver)
{
    uint32_t valid_chunks = 0u;
    uint32_t valid_bytes = 0u;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t mask = 0u;
    uint8_t buffer[64];

    for (uint32_t index = 0; index < receiver->chunk_count; ++index) {
        if (index >= OTA_RESUME_MAX_CHUNKS) {
            break;
        }
        uint32_t length = chunk_length(receiver, index);
        if (length == 0u) {
            break;
        }
        uint32_t chunk_crc = 0xFFFFFFFFu;
        uint32_t combined = crc;
        bool ok = true;

        for (uint32_t offset = 0u; offset < length; offset += sizeof(buffer)) {
            uint32_t n = length - offset;
            if (n > sizeof(buffer)) {
                n = (uint32_t)sizeof(buffer);
            }
            if (receiver->io->flash_read(receiver->io->context,
                                         receiver->target_base +
                                             index * OTA_RESUME_CHUNK_SIZE + offset,
                                         buffer, n) != 0) {
                ok = false;
                break;
            }
            chunk_crc = crc32_ieee(buffer, n, chunk_crc);
            combined = crc32_ieee(buffer, n, combined);
        }
        chunk_crc ^= 0xFFFFFFFFu;
        if (!ok || chunk_crc != receiver->chunk_crcs[index]) {
            break;
        }
        crc = combined;
        valid_bytes += length;
        mask |= (1u << index);
        valid_chunks++;
    }

    receiver->received = valid_bytes;
    receiver->written = valid_bytes;
    receiver->running_crc = crc;
    receiver->chunk_count = valid_chunks;
    receiver->chunk_valid = mask;

    receiver->chunk_index = valid_chunks;
    receiver->chunk_crc32 = 0xFFFFFFFFu;
    receiver->stage_len = 0u;
    receiver->last_progress = 0u;
    mark_prefix_erased(receiver);
}

int ota_receiver_init(ota_receiver_t *receiver, const ota_receiver_io_t *io,
                      uint32_t target_base, uint32_t target_size,
                      uint32_t hardware_id)
{
    if (receiver == NULL || io == NULL || io->flash_erase_page == NULL ||
        io->flash_program == NULL || io->flash_read == NULL) {
        return -1;
    }
    if (target_size > FLASH_SLOT_B_SIZE || target_size < FLASH_PAGE_SIZE) {
        return -1;
    }
    memset(receiver, 0, sizeof(*receiver));
    receiver->io = io;
    receiver->target_base = target_base;
    receiver->target_size = target_size;
    receiver->hardware_id = hardware_id;
    receiver->state = OTA_STATE_IDLE;
    receiver->last_reason = OTA_REASON_NONE;
    receiver->chunk_crc32 = 0xFFFFFFFFu;
    receiver->partial_active = 0u;
    return 0;
}

void ota_receiver_set_installed_version(ota_receiver_t *receiver, uint32_t version)
{
    receiver->previous_version = version;
}

void ota_receiver_attach_metadata(ota_receiver_t *receiver,
                                  const ota_metadata_t *meta)
{
    if (receiver == NULL || meta == NULL) {
        return;
    }
    receiver->base_meta = *meta;
    receiver->has_base_meta = true;
}

void ota_receiver_restore(ota_receiver_t *receiver, const ota_metadata_t *meta)
{
    if (receiver == NULL || meta == NULL) {
        return;
    }
    ota_receiver_attach_metadata(receiver, meta);
    if (!ota_metadata_valid(meta) || meta->state != (uint32_t)OTA_STATE_RECEIVING ||
        meta->image_size == 0u || meta->chunk_count == 0u) {
        return;
    }

    receiver->image_size = meta->image_size;
    receiver->image_crc32 = meta->image_crc32;
    receiver->image_version = meta->image_version;
    receiver->build_id = meta->build_id;
    receiver->hardware_id = meta->hardware_id;
    memcpy(receiver->image_sha256, meta->image_sha256,
           sizeof(receiver->image_sha256));
    memcpy(receiver->image_signature, meta->image_signature,
           sizeof(receiver->image_signature));
    receiver->previous_version =
        (meta->previous_version != 0u) ? meta->previous_version : receiver->previous_version;

    /* Only whole chunks are trusted after a reset. Report the chunk boundary
     * (not the raw received_bytes, which may point into a partially written
     * chunk) so OTA_QUERY and the post-BEGIN offset agree. */
    uint32_t boundary = meta->chunk_count * OTA_RESUME_CHUNK_SIZE;
    if (boundary > meta->image_size) {
        boundary = meta->image_size;
    }
    receiver->received = boundary;
    receiver->written = boundary;
    receiver->running_crc = meta->partial_crc32;
    receiver->chunk_count = meta->chunk_count;
    receiver->chunk_index = meta->chunk_count;
    receiver->chunk_valid = meta->chunk_valid;
    receiver->chunk_crc32 = 0xFFFFFFFFu;
    memcpy(receiver->chunk_crcs, meta->chunk_crc32,
           sizeof(receiver->chunk_crcs));
    receiver->partial_active = 1u;
    receiver->resumable = true;
    receiver->state = OTA_STATE_IDLE;
    receiver->last_reason = OTA_REASON_NONE;
}

static bool manifest_matches(const ota_receiver_t *receiver,
                             const ota_begin_payload_t *begin)
{
    if (!receiver->resumable || receiver->image_size == 0u) {
        return false;
    }
    if (receiver->image_size != begin->image_size ||
        receiver->image_crc32 != begin->image_crc32 ||
        receiver->image_version != begin->version ||
        receiver->build_id != begin->build_id ||
        receiver->hardware_id != begin->hardware_id) {
        return false;
    }
    for (uint32_t i = 0; i < sizeof(receiver->image_sha256); ++i) {
        if (receiver->image_sha256[i] != begin->image_sha256[i]) {
            return false;
        }
    }
    for (uint32_t i = 0; i < sizeof(receiver->image_signature); ++i) {
        if (receiver->image_signature[i] != begin->signature[i]) {
            return false;
        }
    }
    return true;
}

static bool is_all_zero(const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i) {
        if (data[i] != 0u) {
            return false;
        }
    }
    return true;
}

ota_reason_t ota_receiver_begin(ota_receiver_t *receiver,
                                const ota_begin_payload_t *begin, uint32_t now_ms)
{
    if (receiver == NULL || begin == NULL) {
        return OTA_REASON_BAD_STATE;
    }
    if (receiver->state == OTA_STATE_RECEIVING) {
        return OTA_REASON_BAD_STATE;
    }
    if (begin->image_size < 64u || begin->image_size > receiver->target_size) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_SIZE);
        return OTA_REASON_BAD_SIZE;
    }
    if (begin->hardware_id != receiver->hardware_id) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_VERSION);
        return OTA_REASON_BAD_VERSION;
    }
    if (receiver->previous_version != 0u &&
        begin->version < receiver->previous_version) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_VERSION);
        return OTA_REASON_BAD_VERSION;
    }
    /* v0.2.0 requires signed images: an all-zero signature would let an
     * unsigned image slip past the bootloader's Ed25519 gate. */
    if (is_all_zero(begin->signature, sizeof(begin->signature))) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_SIGNATURE);
        return OTA_REASON_BAD_SIGNATURE;
    }

    bool resume = manifest_matches(receiver, begin);

    receiver->image_size = begin->image_size;
    receiver->image_crc32 = begin->image_crc32;

    receiver->image_version = begin->version;
    receiver->build_id = begin->build_id;
    receiver->hardware_id = begin->hardware_id;
    memcpy(receiver->image_sha256, begin->image_sha256,
           sizeof(receiver->image_sha256));
    memcpy(receiver->image_signature, begin->signature,
           sizeof(receiver->image_signature));
    receiver->last_reason = OTA_REASON_NONE;
    receiver->last_activity_ms = now_ms;

    if (resume) {
        verify_persisted_prefix(receiver);
        if (receiver->chunk_count == 0u) {
            /* Persisted chunks did not survive verification: restart cleanly. */
            reset_transfer(receiver);
        } else {
            receiver->partial_active = 1u;
            receiver->resumable = true;
        }
    } else {
        reset_transfer(receiver);
    }

    receiver->resumable = true;
    receiver->chunk_index = receiver->chunk_count;
    receiver->chunk_crc32 = 0xFFFFFFFFu;
    report(receiver, OTA_STATE_RECEIVING, OTA_REASON_NONE);
    return OTA_REASON_NONE;
}

static void finalize_chunk(ota_receiver_t *receiver)
{
    uint32_t index = receiver->chunk_index;
    uint32_t start = index * OTA_RESUME_CHUNK_SIZE;
    uint32_t length = chunk_length(receiver, index);

    /* Nothing to finalise unless the chunk is full and has bytes in it. This
     * also prevents a second finalise when the image ends exactly on a chunk
     * boundary (the index has already advanced to an empty chunk). */
    if (index >= OTA_RESUME_MAX_CHUNKS || length == 0u ||
        receiver->received < start + length || receiver->received == start) {
        return;
    }
    receiver->chunk_crcs[index] = receiver->chunk_crc32 ^ 0xFFFFFFFFu;
    receiver->chunk_valid |= (1u << index);
    receiver->chunk_count = index + 1u;
    receiver->chunk_index = index + 1u;
    receiver->chunk_crc32 = 0xFFFFFFFFu;
}

static void notify_progress(ota_receiver_t *receiver)
{
    uint8_t pct = ota_receiver_percent(receiver);
    if (pct >= receiver->last_progress + 5u) {
        receiver->last_progress = (uint8_t)(pct - (pct % 5u));
        if (receiver->io->notify != NULL) {
            receiver->io->notify(receiver->io->context, OTA_STATE_RECEIVING,
                                 OTA_REASON_NONE, receiver->received,
                                 receiver->image_size, receiver->running_crc);
        }
    }
}

ota_reason_t ota_receiver_data(ota_receiver_t *receiver, uint32_t offset,
                               const uint8_t *data, uint32_t length,
                               uint32_t now_ms)
{
    if (receiver == NULL || data == NULL) {
        return OTA_REASON_BAD_STATE;
    }
    if (receiver->state != OTA_STATE_RECEIVING) {
        return OTA_REASON_BAD_STATE;
    }
    if (offset != receiver->received) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_OUT_OF_ORDER);
        return OTA_REASON_OUT_OF_ORDER;
    }
    if (length == 0u || length > receiver->image_size ||
        offset > receiver->image_size - length) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_SIZE);
        return OTA_REASON_BAD_SIZE;
    }
    if (ensure_page_erased(receiver, offset) != 0 ||
        ensure_page_erased(receiver, offset + length - 1u) != 0) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_FLASH_ERROR);
        return OTA_REASON_FLASH_ERROR;
    }

    receiver->last_activity_ms = now_ms;

    uint32_t remaining = length;
    while (remaining > 0u) {
        uint32_t chunk_start = receiver->chunk_index * OTA_RESUME_CHUNK_SIZE;
        uint32_t chunk_total = chunk_length(receiver, receiver->chunk_index);
        uint32_t chunk_end = chunk_start + chunk_total;
        uint32_t n = chunk_end - receiver->received;
        if (n > remaining) {
            n = remaining;
        }
        if (n == 0u) {
            // Defensive: an empty chunk means the manifest/index is inconsistent.
            report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_SIZE);
            return OTA_REASON_BAD_SIZE;
        }

        receiver->running_crc = crc32_ieee(data, n, receiver->running_crc);
        receiver->chunk_crc32 = crc32_ieee(data, n, receiver->chunk_crc32);

        for (uint32_t i = 0u; i < n; ++i) {
            receiver->stage[receiver->stage_len++] = data[i];
            receiver->received++;
            if (receiver->stage_len == sizeof(receiver->stage)) {
                if (flush_stage(receiver) != 0) {
                    report(receiver, OTA_STATE_ERROR, OTA_REASON_FLASH_ERROR);
                    return OTA_REASON_FLASH_ERROR;
                }
            }
        }

        data += n;
        remaining -= n;

        if (receiver->received == chunk_end && chunk_total != 0u) {
            finalize_chunk(receiver);
            notify_progress(receiver);
            /* Persist at page boundaries only: one metadata write per 8 KB. */
            if (save_meta(receiver, OTA_STATE_RECEIVING, true) != 0) {
                report(receiver, OTA_STATE_ERROR, OTA_REASON_FLASH_ERROR);
                return OTA_REASON_FLASH_ERROR;
            }
        }
    }

    notify_progress(receiver);
    return OTA_REASON_NONE;
}

static bool crc_and_sha_from_flash(ota_receiver_t *receiver,
                                   uint8_t digest[SHA256_DIGEST_SIZE])
{
    uint32_t crc = 0xFFFFFFFFu;
    sha256_t sha;
    uint8_t chunk[64];
    uint32_t offset = 0u;

    sha256_init(&sha);
    while (offset < receiver->image_size) {
        uint32_t n = receiver->image_size - offset;
        if (n > sizeof(chunk)) {
            n = (uint32_t)sizeof(chunk);
        }
        if (receiver->io->flash_read(receiver->io->context,
                                     receiver->target_base + offset, chunk, n) != 0) {
            return false;
        }
        crc = crc32_ieee(chunk, n, crc);
        sha256_update(&sha, chunk, n);
        offset += n;
    }
    crc ^= 0xFFFFFFFFu;
    sha256_final(&sha, digest);
    return crc == receiver->image_crc32;
}

ota_reason_t ota_receiver_end(ota_receiver_t *receiver, const ota_end_payload_t *end,
                              uint32_t now_ms)
{
    uint8_t digest[SHA256_DIGEST_SIZE];

    if (receiver == NULL || end == NULL) {
        return OTA_REASON_BAD_STATE;
    }
    if (receiver->state != OTA_STATE_RECEIVING) {
        return OTA_REASON_BAD_STATE;
    }
    receiver->state = OTA_STATE_VERIFYING;
    receiver->last_activity_ms = now_ms;

    /* Bytes in the staging buffer belong to the image and must be committed
     * before the length check. */
    if (flush_stage(receiver) != 0) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_FLASH_ERROR);
        return OTA_REASON_FLASH_ERROR;
    }

    if (receiver->received != receiver->image_size) {
        receiver->partial_active = 1u;
        (void)save_meta(receiver, OTA_STATE_RECEIVING, true);
        report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_SIZE);
        return OTA_REASON_BAD_SIZE;
    }

    finalize_chunk(receiver);

    if (end->image_size != receiver->image_size ||
        end->image_crc32 != receiver->image_crc32) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_CRC);
        return OTA_REASON_BAD_CRC;
    }

    if (!crc_and_sha_from_flash(receiver, digest)) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_CRC);
        return OTA_REASON_BAD_CRC;
    }
    for (uint32_t i = 0u; i < SHA256_DIGEST_SIZE; ++i) {
        if (digest[i] != receiver->image_sha256[i]) {
            report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_CRC);
            return OTA_REASON_BAD_CRC;
        }
    }

    if (save_meta(receiver, OTA_STATE_READY_TO_BOOT, true) != 0) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_FLASH_ERROR);
        return OTA_REASON_FLASH_ERROR;
    }

    receiver->last_progress = 100u;
    receiver->partial_active = 0u;
    report(receiver, OTA_STATE_READY_TO_BOOT, OTA_REASON_NONE);
    return OTA_REASON_NONE;
}

void ota_receiver_abort(ota_receiver_t *receiver, ota_reason_t reason,
                        uint32_t now_ms)
{
    if (receiver == NULL) {
        return;
    }
    receiver->last_activity_ms = now_ms;
    receiver->stage_len = 0u;
    receiver->partial_active = 0u;
    receiver->resumable = false;
    receiver->chunk_count = 0u;
    receiver->chunk_valid = 0u;
    receiver->received = 0u;
    receiver->written = 0u;
    receiver->running_crc = 0xFFFFFFFFu;
    if (receiver->state == OTA_STATE_RECEIVING ||
        receiver->state == OTA_STATE_VERIFYING) {
        (void)save_meta(receiver, OTA_STATE_ERROR, false);
        report(receiver, OTA_STATE_ERROR, reason);
    }
}

void ota_receiver_suspend(ota_receiver_t *receiver, ota_reason_t reason,
                          uint32_t now_ms)
{
    if (receiver == NULL) {
        return;
    }
    receiver->last_activity_ms = now_ms;
    if (receiver->state != OTA_STATE_RECEIVING &&
        receiver->state != OTA_STATE_VERIFYING) {
        return;
    }

    /* Bytes still in the 16-byte staging buffer never reached flash, so they
     * must not be counted as part of the resumable prefix. */
    receiver->stage_len = 0u;
    receiver->received = (receiver->written <= receiver->image_size)
                             ? receiver->written
                             : receiver->image_size;
    /* The last partially completed chunk cannot be trusted after trimming. */
    receiver->chunk_count = receiver->chunk_index;
    receiver->chunk_crc32 = 0xFFFFFFFFu;
    receiver->partial_active = 1u;
    receiver->resumable = true;
    (void)save_meta(receiver, OTA_STATE_RECEIVING, true);
    report(receiver, OTA_STATE_ERROR, reason);
}

void ota_receiver_tick(ota_receiver_t *receiver, uint32_t now_ms)
{
    if (receiver == NULL) {
        return;
    }
    if (receiver->state == OTA_STATE_RECEIVING &&
        (uint32_t)(now_ms - receiver->last_activity_ms) > OTA_RECEIVE_TIMEOUT_MS) {
        ota_receiver_suspend(receiver, OTA_REASON_TIMEOUT, now_ms);
    }
}

uint8_t ota_receiver_percent(const ota_receiver_t *receiver)
{
    if (receiver->image_size == 0u) {
        return 0u;
    }
    uint32_t pct = (receiver->received * 100u) / receiver->image_size;
    return (uint8_t)(pct > 100u ? 100u : pct);
}

void ota_receiver_build_info(const ota_receiver_t *receiver,
                             ota_info_payload_t *info)
{
    memset(info, 0, sizeof(*info));
    info->state = (uint8_t)receiver->state;
    info->reason = (uint8_t)receiver->last_reason;
    info->received = receiver->received;
    info->total = receiver->image_size;
    info->image_crc32 = receiver->image_crc32;
    info->version = receiver->image_version;
    info->chunk_size = OTA_RESUME_CHUNK_SIZE;
    info->chunk_count = receiver->chunk_count;
    info->hardware_id = receiver->hardware_id;
}
