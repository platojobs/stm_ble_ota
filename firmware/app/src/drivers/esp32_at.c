/**
 * @file    esp32_at.c
 * @brief   Line-oriented ESP-AT engine.
 */

#include "esp32_at.h"
#include "bsp_time.h"

#include <stdio.h>
#include <string.h>

static void append_response(esp_at_t *at, const char *line, uint16_t length)
{
    if (at->response_len + length + 1u >= ESP_AT_RESPONSE_MAX) {
        return;
    }
    memcpy(&at->response[at->response_len], line, length);
    at->response_len = (uint16_t)(at->response_len + length);
    at->response[at->response_len++] = '\n';
    at->response[at->response_len] = '\0';
}

static void dispatch_line(esp_at_t *at, const char *line)
{
    if (line[0] == '\0') {
        return;
    }

    if (strcmp(line, "OK") == 0) {
        at->got_ok = true;
        return;
    }
    if (strcmp(line, "ERROR") == 0 || strcmp(line, "FAIL") == 0 ||
        strcmp(line, "SEND FAIL") == 0) {
        at->got_error = true;
        ++at->error_count;
        return;
    }
    if (strcmp(line, "busy p...") == 0 || strcmp(line, "busy m...") == 0) {
        at->got_error = true;
        return;
    }

    if (line[0] == '+') {
        esp_at_event_t event;
        esp_at_event_type_t type = esp_at_parse_line(line, &event);
        if (type != ESP_AT_EVT_UNKNOWN) {
            ++at->urc_count;
            if (at->urc_callback != NULL) {
                at->urc_callback(&event, at->urc_context);
            }
        }
        append_response(at, line, (uint16_t)strlen(line));
        return;
    }

    append_response(at, line, (uint16_t)strlen(line));
}

void esp_at_init(esp_at_t *at, bsp_uart_id_t uart, esp_at_urc_cb_t callback,
                 void *context)
{
    memset(at, 0, sizeof(*at));
    at->uart = uart;
    at->urc_callback = callback;
    at->urc_context = context;
    at->response[0] = '\0';
    bsp_uart_flush(uart);
}

void esp_at_poll(esp_at_t *at)
{
    uint8_t byte;
    while (bsp_uart_read_byte(at->uart, &byte)) {
        if (byte == '\r') {
            continue;
        }
        if (byte == '\n') {
            if (at->line_len > 0u) {
                at->line[at->line_len] = '\0';
                dispatch_line(at, at->line);
                at->line_len = 0u;
            }
            continue;
        }
        if (byte == '>') {
            at->got_prompt = true;
            continue;
        }
        if (at->line_len + 1u < ESP_AT_LINE_MAX) {
            at->line[at->line_len++] = (char)byte;
        } else {
            at->line_overflow = true;
            at->line_len = 0u;
        }
    }
}

static void reset_transaction(esp_at_t *at)
{
    at->got_ok = false;
    at->got_error = false;
    at->got_prompt = false;
    at->line_overflow = false;
    at->response_len = 0u;
    at->response[0] = '\0';
}

esp_at_exec_result_t esp_at_exec(esp_at_t *at, const char *command,
                                 uint32_t timeout_ms, bool expect_prompt)
{
    if (command == NULL) {
        return ESP_AT_EXEC_ERROR;
    }

    reset_transaction(at);
    // Drain anything left over from a previous URC before starting.
    esp_at_poll(at);

    if (bsp_uart_write_str(at->uart, command) != 0 ||
        bsp_uart_write_str(at->uart, "\r\n") != 0) {
        return ESP_AT_EXEC_ERROR;
    }

    uint32_t start = bsp_time_ms();
    for (;;) {
        esp_at_poll(at);
        if (at->got_ok) {
            return ESP_AT_EXEC_OK;
        }
        if (at->got_error) {
            return ESP_AT_EXEC_ERROR;
        }
        if (expect_prompt && at->got_prompt) {
            return ESP_AT_EXEC_PROMPT;
        }
        if ((bsp_time_ms() - start) >= timeout_ms) {
            return ESP_AT_EXEC_TIMEOUT;
        }
    }
}

esp_at_exec_result_t esp_at_wait_result(esp_at_t *at, uint32_t timeout_ms)
{
    uint32_t start = bsp_time_ms();
    for (;;) {
        esp_at_poll(at);
        if (at->got_ok) {
            return ESP_AT_EXEC_OK;
        }
        if (at->got_error) {
            return ESP_AT_EXEC_ERROR;
        }
        if ((bsp_time_ms() - start) >= timeout_ms) {
            return ESP_AT_EXEC_TIMEOUT;
        }
    }
}

const char *esp_at_last_response(const esp_at_t *at)
{
    return at->response;
}

int esp_at_ble_notify(esp_at_t *at, uint8_t conn, uint8_t service,
                      uint8_t characteristic, const void *payload,
                      uint16_t length, uint32_t timeout_ms)
{
    if (payload == NULL || length == 0u || length > 512u) {
        return -1;
    }

    char command[48];
    (void)snprintf(command, sizeof(command), "AT+BLEGATTSNTFY=%u,%u,%u,%u",
                   (unsigned)conn, (unsigned)service, (unsigned)characteristic,
                   (unsigned)length);

    esp_at_exec_result_t result = esp_at_exec(at, command, timeout_ms, true);
    if (result != ESP_AT_EXEC_PROMPT) {
        return -1;
    }

    if (bsp_uart_write(at->uart, payload, length) != 0) {
        return -1;
    }

    // The module acknowledges the payload with "OK" once it has been queued.
    return (esp_at_wait_result(at, timeout_ms) == ESP_AT_EXEC_OK) ? 0 : -1;
}
