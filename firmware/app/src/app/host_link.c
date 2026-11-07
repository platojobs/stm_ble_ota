// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    host_link.c
 * @brief   USART2 text/binary demultiplexer for the PC test tool.
 */

#include "host_link.h"
#include "bsp_uart.h"
#include "ota_protocol.h"

#include <string.h>

#define HOST_LINE_MAX 160u
#define FRAME_BYTE_LIMIT (STM_FRAME_MAX_SIZE + 32u)

static host_json_cb_t g_json_cb;
static host_frame_cb_t g_frame_cb;
static void *g_context;

static stm_frame_parser_t g_parser;
static char g_line[HOST_LINE_MAX];
static uint16_t g_line_len;

static bool g_in_frame;
static bool g_have_sync;
static uint32_t g_frame_bytes;
static uint32_t g_frames_seen;

static void dispatch_line(void)
{
    if (g_line_len == 0u) {
        return;
    }
    g_line[g_line_len] = '\0';
    if (g_json_cb != NULL) {
        g_json_cb(g_line, g_context);
    }
    g_line_len = 0u;
}

void host_link_init(host_json_cb_t json_cb, host_frame_cb_t frame_cb,
                    void *context)
{
    g_json_cb = json_cb;
    g_frame_cb = frame_cb;
    g_context = context;
    g_line_len = 0u;
    g_in_frame = false;
    g_have_sync = false;
    g_frame_bytes = 0u;
    g_frames_seen = 0u;
    stm_frame_parser_init(&g_parser);
    bsp_uart_flush(BSP_UART_HOST);
}

void host_link_poll(void)
{
    uint8_t byte;
    while (bsp_uart_read_byte(BSP_UART_HOST, &byte)) {
        if (g_in_frame) {
            (void)stm_frame_parser_feed(&g_parser, &byte, 1u, g_frame_cb,
                                        g_context);
            ++g_frame_bytes;
            if (g_parser.frames_ok != g_frames_seen) {
                g_frames_seen = g_parser.frames_ok;
                g_in_frame = false;
                g_frame_bytes = 0u;
            } else if (g_frame_bytes > FRAME_BYTE_LIMIT) {
                /* No complete frame after a full frame's worth of bytes:
                 * abandon binary mode and resynchronise on text. */
                g_in_frame = false;
                g_frame_bytes = 0u;
                stm_frame_parser_init(&g_parser);
            }
            continue;
        }

        if (g_have_sync) {
            if (byte == STM_FRAME_SYNC1) {
                uint8_t sync[2] = { STM_FRAME_SYNC0, STM_FRAME_SYNC1 };
                stm_frame_parser_init(&g_parser);
                (void)stm_frame_parser_feed(&g_parser, sync, 2u, g_frame_cb,
                                            g_context);
                g_frames_seen = g_parser.frames_ok;
                g_in_frame = true;
                g_frame_bytes = 2u;
            } else {
                g_have_sync = false;
                if (byte == STM_FRAME_SYNC0) {
                    g_have_sync = true;
                }
            }
            continue;
        }

        if (byte == STM_FRAME_SYNC0) {
            g_have_sync = true;
            continue;
        }
        if (byte == '\r') {
            continue;
        }
        if (byte == '\n') {
            dispatch_line();
            continue;
        }
        if (g_line_len + 1u < HOST_LINE_MAX) {
            g_line[g_line_len++] = (char)byte;
        } else {
            g_line_len = 0u; /* oversized line discarded */
        }
    }
}

void host_link_send_json(const char *json)
{
    if (json == NULL) {
        return;
    }
    (void)bsp_uart_write_str(BSP_UART_HOST, json);
    (void)bsp_uart_write_str(BSP_UART_HOST, "\r\n");
}

void host_link_send_raw(const void *data, uint32_t length)
{
    if (data != NULL) {
        (void)bsp_uart_write(BSP_UART_HOST, data, length);
    }
}
