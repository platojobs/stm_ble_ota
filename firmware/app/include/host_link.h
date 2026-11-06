/**
 * @file    host_link.h
 * @brief   Direct PC link on the ST-LINK virtual COM port (USART2).
 *
 * The link carries two kinds of traffic:
 *   - newline-terminated JSON commands/responses (provisioning, info),
 *   - binary OTA frames beginning with 0xA5 0x5A.
 *
 * This gives the PC test tool a complete provisioning + OTA path that does not
 * depend on the BLE co-processor custom firmware.
 */

#ifndef HOST_LINK_H
#define HOST_LINK_H

#include <stdint.h>
#include <stddef.h>

#include "ota_protocol.h"

typedef void (*host_json_cb_t)(const char *json, void *context);
typedef void (*host_frame_cb_t)(const stm_frame_t *frame, void *context);

void host_link_init(host_json_cb_t json_cb, host_frame_cb_t frame_cb,
                    void *context);

void host_link_poll(void);

/** Send a JSON line (CRLF appended) to the PC. */
void host_link_send_json(const char *json);

/** Send raw bytes (used for binary status if needed). */
void host_link_send_raw(const void *data, uint32_t length);

#endif /* HOST_LINK_H */
