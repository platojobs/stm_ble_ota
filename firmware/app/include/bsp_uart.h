// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    bsp_uart.h
 * @brief   Interrupt-driven UART abstraction (ESP32 link and PC test link).
 *
 * Received bytes are buffered in 256-byte ring buffers by the USART interrupt.
 * The application polls with bsp_uart_read_byte(), so no callback re-enters
 * higher-level protocol code from interrupt context.
 */

#ifndef BSP_UART_H
#define BSP_UART_H

#include "app_config.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    BSP_UART_ESP = 0,   /* USART1, ESP32 AT firmware            */
    BSP_UART_HOST = 1,  /* USART2, ST-LINK virtual COM port     */
    BSP_UART_COUNT
} bsp_uart_id_t;

void bsp_uart_init(void);

/** Blocking write; returns 0 on success. */
int bsp_uart_write(bsp_uart_id_t id, const void *data, uint32_t length);
int bsp_uart_write_str(bsp_uart_id_t id, const char *text);

/** Non-blocking read of one byte. Returns true when a byte was consumed. */
bool bsp_uart_read_byte(bsp_uart_id_t id, uint8_t *out);

/** Number of bytes currently buffered. */
uint32_t bsp_uart_available(bsp_uart_id_t id);

/** Drop all buffered bytes (used before a synchronous AT transaction). */
void bsp_uart_flush(bsp_uart_id_t id);

#endif /* BSP_UART_H */
