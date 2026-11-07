// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    ota_service.h
 * @brief   Application-side OTA glue: frame parsing, flash writes, progress.
 *
 * Incoming bytes may be 20-byte BLE writes or larger UART frames. They are
 * reassembled by stm_frame_parser_t before being handed to ota_receiver_t,
 * which writes slot B and persists the READY_TO_BOOT metadata record.
 * Progress and final results are emitted as STM_FRAME_OTA_PROGRESS /
 * STM_FRAME_OTA_RESULT frames toward the host.
 */

#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include "ota_protocol.h"

#include <stdint.h>
#include <stdbool.h>

typedef void (*ota_notify_cb_t)(const uint8_t *frame, uint32_t length,
                                void *context);

int ota_service_init(void);
void ota_service_set_notify_callback(ota_notify_cb_t callback, void *context);

/** Feed raw bytes (any fragmentation) from BLE or UART. */
void ota_service_on_bytes(const uint8_t *data, uint32_t length);

/** Feed an already parsed frame (direct PC link). */
void ota_service_on_frame(const stm_frame_t *frame);

/** Periodic: transfer timeout and the post-download reset. */
void ota_service_tick(uint32_t now_ms);

/** Version currently running (for device info and downgrade checks). */
uint32_t ota_service_running_version(void);

/** State machine inspection for the device-info JSON. */
uint8_t ota_service_state(void);
uint8_t ota_service_last_reason(void);

#endif /* OTA_SERVICE_H */
