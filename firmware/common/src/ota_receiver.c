#include "ota_receiver.h"
#include "flash_layout.h"
#include "crc.h"

#include <string.h>

#define OTA_RECEIVE_TIMEOUT_MS  10000u
#define OTA_PAGE_COUNT          (FLASH_SLOT_B_SIZE / FLASH_PAGE_SIZE)

static void report(ota_receiver_t *receiver, ota_state_t state, ota_reason_t reason)
{
    receiver->state = state;
    receiver->last_reason = reason;
    if (receiver->io->notify != NULL) {
        receiver->io->notify(receiver->io->context, state, reason, receiver->received,
                             receiver->image_size, receiver->running_crc);
    }
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
                                       receiver->target_base + page * FLASH_PAGE_SIZE) != 0) {
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
    /* Pad the final granule with 0xFF: ECC flash is programmed in 16 bytes. */
    if (receiver->stage_len < 16u) {
        memset(receiver->stage + receiver->stage_len, 0xFF, 16u - receiver->stage_len);
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
    return 0;
}

void ota_receiver_set_installed_version(ota_receiver_t *receiver, uint32_t version)
{
    receiver->previous_version = version;
}

uint8_t ota_receiver_percent(const ota_receiver_t *receiver)
{
    if (receiver->image_size == 0u) {
        return 0u;
    }
    uint32_t pct = (receiver->received * 100u) / receiver->image_size;
    return (uint8_t)(pct > 100u ? 100u : pct);
}

static void notify_progress_if_needed(ota_receiver_t *receiver, uint32_t now_ms)
{
    receiver->last_activity_ms = now_ms;
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

ota_reason_t ota_receiver_begin(ota_receiver_t *receiver, const ota_begin_payload_t *begin,
                                uint32_t now_ms)
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

    receiver->image_size = begin->image_size;
    receiver->image_crc32 = begin->image_crc32;
    receiver->image_version = begin->version;
    receiver->build_id = begin->build_id;
    receiver->received = 0u;
    receiver->written = 0u;
    receiver->running_crc = 0xFFFFFFFFu;
    receiver->erased_pages = 0u;
    receiver->last_progress = 0u;
    receiver->stage_len = 0u;
    receiver->last_activity_ms = now_ms;
    receiver->last_reason = OTA_REASON_NONE;

    report(receiver, OTA_STATE_RECEIVING, OTA_REASON_NONE);
    return OTA_REASON_NONE;
}

ota_reason_t ota_receiver_data(ota_receiver_t *receiver, uint32_t offset,
                               const uint8_t *data, uint32_t length, uint32_t now_ms)
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
    if (length == 0u || offset + length > receiver->image_size) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_SIZE);
        return OTA_REASON_BAD_SIZE;
    }
    if (ensure_page_erased(receiver, offset) != 0 ||
        ensure_page_erased(receiver, offset + length - 1u) != 0) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_FLASH_ERROR);
        return OTA_REASON_FLASH_ERROR;
    }

    receiver->running_crc = crc32_ieee(data, length, receiver->running_crc);

    for (uint32_t i = 0; i < length; ++i) {
        receiver->stage[receiver->stage_len++] = data[i];
        receiver->received++;
        if (receiver->stage_len == sizeof(receiver->stage)) {
            if (flush_stage(receiver) != 0) {
                report(receiver, OTA_STATE_ERROR, OTA_REASON_FLASH_ERROR);
                return OTA_REASON_FLASH_ERROR;
            }
        }
    }

    notify_progress_if_needed(receiver, now_ms);
    return OTA_REASON_NONE;
}

static uint32_t crc_from_flash(ota_receiver_t *receiver)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t chunk[64];
    uint32_t offset = 0u;

    while (offset < receiver->image_size) {
        uint32_t n = receiver->image_size - offset;
        if (n > sizeof(chunk)) {
            n = (uint32_t)sizeof(chunk);
        }
        if (receiver->io->flash_read(receiver->io->context,
                                     receiver->target_base + offset, chunk, n) != 0) {
            return 0u;
        }
        crc = crc32_ieee(chunk, n, crc);
        offset += n;
    }
    return crc ^ 0xFFFFFFFFu;
}

ota_reason_t ota_receiver_end(ota_receiver_t *receiver, const ota_end_payload_t *end,
                              uint32_t now_ms)
{
    uint32_t final_crc;

    if (receiver == NULL || end == NULL) {
        return OTA_REASON_BAD_STATE;
    }
    if (receiver->state != OTA_STATE_RECEIVING) {
        return OTA_REASON_BAD_STATE;
    }
    receiver->last_activity_ms = now_ms;
    receiver->state = OTA_STATE_VERIFYING;

    if (end->image_size != receiver->image_size ||
        receiver->received != receiver->image_size) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_SIZE);
        return OTA_REASON_BAD_SIZE;
    }

    final_crc = receiver->running_crc ^ 0xFFFFFFFFu;
    if (end->image_crc32 != receiver->image_crc32 || final_crc != end->image_crc32) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_CRC);
        return OTA_REASON_BAD_CRC;
    }

    if (flush_stage(receiver) != 0) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_FLASH_ERROR);
        return OTA_REASON_FLASH_ERROR;
    }

    /* Verify what actually landed in flash, not just the stream CRC. */
    uint32_t flash_crc = crc_from_flash(receiver);
    if (flash_crc != receiver->image_crc32) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_BAD_CRC);
        return OTA_REASON_BAD_CRC;
    }

    ota_metadata_t meta;
    ota_metadata_default(&meta);
    meta.state = OTA_STATE_READY_TO_BOOT;
    meta.image_size = receiver->image_size;
    meta.image_crc32 = receiver->image_crc32;
    meta.image_version = receiver->image_version;
    meta.install_attempts = 0u;
    meta.previous_version = receiver->previous_version;

    if (receiver->io->metadata_save != NULL &&
        receiver->io->metadata_save(receiver->io->context, &meta) != 0) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_FLASH_ERROR);
        return OTA_REASON_FLASH_ERROR;
    }

    receiver->last_progress = 100u;
    report(receiver, OTA_STATE_READY_TO_BOOT, OTA_REASON_NONE);
    return OTA_REASON_NONE;
}

void ota_receiver_abort(ota_receiver_t *receiver, ota_reason_t reason, uint32_t now_ms)
{
    if (receiver == NULL) {
        return;
    }
    receiver->last_activity_ms = now_ms;
    if (receiver->state == OTA_STATE_RECEIVING || receiver->state == OTA_STATE_VERIFYING) {
        report(receiver, OTA_STATE_ERROR, reason);
    }
}

void ota_receiver_tick(ota_receiver_t *receiver, uint32_t now_ms)
{
    if (receiver == NULL) {
        return;
    }
    if (receiver->state == OTA_STATE_RECEIVING &&
        (uint32_t)(now_ms - receiver->last_activity_ms) > OTA_RECEIVE_TIMEOUT_MS) {
        report(receiver, OTA_STATE_ERROR, OTA_REASON_TIMEOUT);
    }
}
