/**
 * @file    esp32_at.h
 * @brief   ESP32 AT command engine for the WiFi/BLE co-processor.
 *
 * The engine owns one UART and turns it into:
 *   - synchronous command execution (wait for "OK", "ERROR" or the ">" prompt),
 *   - unsolicited result code (URC) dispatch to a callback,
 *   - BLE notification writes that need the two-phase ">" + payload handshake.
 *
 * Lines are assembled from the UART ring buffer in bsp_uart.c; this code always
 * runs from the main loop, never from an interrupt.
 */

#ifndef ESP32_AT_H
#define ESP32_AT_H

#include "bsp_uart.h"
#include "esp_at_parser.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ESP_AT_LINE_MAX        192u
#define ESP_AT_RESPONSE_MAX    384u

typedef enum {
    ESP_AT_EXEC_OK = 0,
    ESP_AT_EXEC_ERROR = -1,
    ESP_AT_EXEC_TIMEOUT = -2,
    ESP_AT_EXEC_PROMPT = 1,
} esp_at_exec_result_t;

typedef void (*esp_at_urc_cb_t)(const esp_at_event_t *event, void *context);

typedef struct {
    bsp_uart_id_t uart;
    esp_at_urc_cb_t urc_callback;
    void *urc_context;

    char line[ESP_AT_LINE_MAX];
    uint16_t line_len;

    char response[ESP_AT_RESPONSE_MAX];
    uint16_t response_len;

    bool got_ok;
    bool got_error;
    bool got_prompt;
    bool line_overflow;

    uint32_t urc_count;
    uint32_t error_count;
} esp_at_t;

/** Bind the engine to a UART and register the URC callback. */
void esp_at_init(esp_at_t *at, bsp_uart_id_t uart, esp_at_urc_cb_t callback,
                 void *context);

/** Consume all currently buffered UART bytes. Call it regularly. */
void esp_at_poll(esp_at_t *at);

/**
 * Send @p command (CRLF is appended) and wait for a final result.
 * @param expect_prompt when true, a '>' prompt also ends the wait successfully.
 */
esp_at_exec_result_t esp_at_exec(esp_at_t *at, const char *command,
                                 uint32_t timeout_ms, bool expect_prompt);

/** Wait for "OK"/"ERROR" without sending anything (used after payload upload). */
esp_at_exec_result_t esp_at_wait_result(esp_at_t *at, uint32_t timeout_ms);

/** Last accumulated non-URC response text (NUL terminated). */
const char *esp_at_last_response(const esp_at_t *at);

/**
 * Two-phase BLE notification: command, wait for '>', send payload, wait OK.
 * The caller must split the payload to the negotiated MTU (20 bytes is safe).
 */
int esp_at_ble_notify(esp_at_t *at, uint8_t conn, uint8_t service,
                      uint8_t characteristic, const void *payload,
                      uint16_t length, uint32_t timeout_ms);

#endif /* ESP32_AT_H */
