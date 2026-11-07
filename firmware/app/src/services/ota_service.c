// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    ota_service.c
 * @brief   Connects the resumable OTA receiver to slot B, metadata and host
 *          notifications.
 */

#include "ota_service.h"
#include "ota_receiver.h"
#include "ota_metadata.h"
#include "record_store.h"
#include "bsp_flash.h"
#include "bsp_time.h"
#include "app_config.h"
#include "flash_layout.h"
#include "stm32c562xx.h"

#include <stddef.h>
#include <string.h>

static record_store_t g_meta_store;
static ota_receiver_t g_receiver;
static ota_receiver_io_t g_io;
static stm_frame_parser_t g_parser;
static ota_notify_cb_t g_notify_cb;
static void *g_notify_context;
static uint32_t g_running_version;
static bool g_reset_pending;
static uint32_t g_reset_at_ms;

static int record_erase(void *context, uint32_t page_address)
{
    (void)context;
    return bsp_flash_erase_page(page_address);
}

static int record_program(void *context, uint32_t address, const void *data,
                          uint32_t length)
{
    (void)context;
    return bsp_flash_program(address, data, length);
}

static int record_read(void *context, uint32_t address, void *out, uint32_t length)
{
    (void)context;
    return bsp_flash_read(address, out, length);
}

static int metadata_save(void *context, const ota_metadata_t *metadata)
{
    (void)context;
    return ota_metadata_write(&g_meta_store, metadata);
}

static void send_frame(uint8_t type, const void *payload, uint16_t length)
{
    if (g_notify_cb == NULL) {
        return;
    }
    uint8_t frame[STM_FRAME_MAX_SIZE];
    uint32_t size = stm_frame_build(type, 0u, 0u, 0u, payload, length, frame,
                                    sizeof(frame));
    if (size > 0u) {
        g_notify_cb(frame, size, g_notify_context);
    }
}

static void send_progress(ota_state_t state, ota_reason_t reason,
                          uint32_t received, uint32_t total, uint32_t crc32)
{
    ota_progress_payload_t progress;
    progress.state = (uint8_t)state;
    progress.reason = (uint8_t)reason;
    progress.reserved = 0u;
    progress.received = received;
    progress.total = total;
    progress.crc32 = crc32;
    send_frame(STM_FRAME_OTA_PROGRESS, &progress, sizeof(progress));
}

static void send_info(void)
{
    ota_info_payload_t info;
    ota_receiver_build_info(&g_receiver, &info);
    send_frame(STM_FRAME_OTA_INFO, &info, sizeof(info));
}

static void receiver_notify(void *context, ota_state_t state, ota_reason_t reason,
                            uint32_t received, uint32_t total, uint32_t crc32)
{
    (void)context;
    send_progress(state, reason, received, total, crc32);
}

static void handle_frame(const stm_frame_t *frame, void *context)
{
    (void)context;
    uint32_t now = bsp_time_ms();
    ota_reason_t reason = OTA_REASON_NONE;

    switch ((stm_frame_type_t)frame->type) {
    case STM_FRAME_OTA_QUERY:
        send_info();
        break;
    case STM_FRAME_OTA_BEGIN: {
        if (frame->payload_len != sizeof(ota_begin_payload_t)) {
            send_progress(OTA_STATE_ERROR, OTA_REASON_BAD_SIZE, 0u, 0u, 0u);
            return;
        }
        ota_begin_payload_t begin;
        memcpy(&begin, frame->payload, sizeof(begin));
        reason = ota_receiver_begin(&g_receiver, &begin, now);
        if (reason != OTA_REASON_NONE) {
            send_progress(OTA_STATE_ERROR, reason, g_receiver.received,
                          g_receiver.image_size, g_receiver.running_crc);
        }
        break;
    }
    case STM_FRAME_OTA_DATA: {
        reason = ota_receiver_data(&g_receiver, frame->offset, frame->payload,
                                   frame->payload_len, now);
        if (reason != OTA_REASON_NONE) {
            send_progress(OTA_STATE_ERROR, reason, g_receiver.received,
                          g_receiver.image_size, g_receiver.running_crc);
        }
        break;
    }
    case STM_FRAME_OTA_END: {
        if (frame->payload_len != sizeof(ota_end_payload_t)) {
            send_progress(OTA_STATE_ERROR, OTA_REASON_BAD_SIZE, 0u, 0u, 0u);
            return;
        }
        ota_end_payload_t end;
        memcpy(&end, frame->payload, sizeof(end));
        reason = ota_receiver_end(&g_receiver, &end, now);
        send_progress(g_receiver.state, reason, g_receiver.received,
                      g_receiver.image_size, g_receiver.running_crc);
        if (reason == OTA_REASON_NONE) {
            send_frame(STM_FRAME_OTA_RESULT, &end, sizeof(end));
            g_reset_pending = true;
            g_reset_at_ms = now + OTA_RESET_DELAY_MS;
        }
        break;
    }
    case STM_FRAME_OTA_ABORT:
        ota_receiver_abort(&g_receiver, OTA_REASON_ABORTED, now);
        send_progress(OTA_STATE_ERROR, OTA_REASON_ABORTED, g_receiver.received,
                      g_receiver.image_size, g_receiver.running_crc);
        break;
    default:
        break;
    }
}

int ota_service_init(void)
{
    memset(&g_receiver, 0, sizeof(g_receiver));
    g_reset_pending = false;

    record_flash_if_t record_if = {
        .context = NULL,
        .erase_page = record_erase,
        .program = record_program,
        .read = record_read,
    };
    if (record_store_init(&g_meta_store, &record_if, FLASH_META_PAGE0,
                          FLASH_META_PAGE1, sizeof(ota_metadata_t)) != 0) {
        return -1;
    }

    ota_metadata_t meta;
    bool found = false;
    ota_metadata_load_or_default(&g_meta_store, &meta, &found);
    g_running_version = APP_VERSION;
    if (found && meta.state == OTA_STATE_INSTALLED &&
        meta.image_version != 0u) {
        g_running_version = meta.image_version;
        if (meta.running_version == 0u) {
            ota_metadata_promote_running(&meta);
            (void)ota_metadata_write(&g_meta_store, &meta);
        }
    }

    g_io.context = NULL;
    g_io.flash_erase_page = record_erase;
    g_io.flash_program = record_program;
    g_io.flash_read = record_read;
    g_io.metadata_save = metadata_save;
    g_io.notify = receiver_notify;

    if (ota_receiver_init(&g_receiver, &g_io, FLASH_SLOT_B_START,
                          FLASH_SLOT_B_SIZE, APP_HARDWARE_ID) != 0) {
        return -1;
    }
    ota_receiver_set_installed_version(&g_receiver, g_running_version);
    ota_receiver_attach_metadata(&g_receiver, &meta);
    ota_receiver_restore(&g_receiver, &meta);

    stm_frame_parser_init(&g_parser);
    return 0;
}

void ota_service_set_notify_callback(ota_notify_cb_t callback, void *context)
{
    g_notify_cb = callback;
    g_notify_context = context;
}

void ota_service_on_bytes(const uint8_t *data, uint32_t length)
{
    if (data == NULL) {
        return;
    }
    (void)stm_frame_parser_feed(&g_parser, data, length, handle_frame, NULL);
}

void ota_service_on_frame(const stm_frame_t *frame)
{
    if (frame != NULL) {
        handle_frame(frame, NULL);
    }
}

void ota_service_tick(uint32_t now_ms)
{
    ota_receiver_tick(&g_receiver, now_ms);

    if (g_reset_pending && (int32_t)(now_ms - g_reset_at_ms) >= 0) {
        g_reset_pending = false;
        /* Enter the bootloader: it sees READY_TO_BOOT and installs slot B. */
        NVIC_SystemReset();
    }
}

void ota_service_on_ble_disconnect(void)
{
    /* A BLE drop in the middle of a transfer is a recoverable event: persist
     * the partial prefix so the next connection can resume. */
    ota_receiver_suspend(&g_receiver, OTA_REASON_TIMEOUT, bsp_time_ms());
}

uint32_t ota_service_running_version(void)
{
    return g_running_version;
}

uint8_t ota_service_state(void)
{
    return (uint8_t)g_receiver.state;
}

uint8_t ota_service_last_reason(void)
{
    return (uint8_t)g_receiver.last_reason;
}

err_code_t ota_service_last_error_code(void)
{
    return err_code_from_ota_reason(g_receiver.last_reason);
}

uint32_t ota_service_received(void)
{
    return g_receiver.received;
}

uint32_t ota_service_total(void)
{
    return g_receiver.image_size;
}

void ota_service_abort_transfer(void)
{
    ota_receiver_abort(&g_receiver, OTA_REASON_ABORTED, bsp_time_ms());
}

void ota_service_confirm_boot(void)
{
    ota_metadata_t meta;
    bool found = false;
    ota_metadata_load_or_default(&g_meta_store, &meta, &found);
    if (!found) {
        ota_metadata_default(&meta);
    }
    if (meta.boot_attempts != 0u) {
        meta.boot_attempts = 0u;
        (void)ota_metadata_write(&g_meta_store, &meta);
    }
}

void ota_service_factory_reset(void)
{
    ota_metadata_t meta;
    ota_metadata_default(&meta);
    (void)ota_metadata_write(&g_meta_store, &meta);
}
