// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "esp_at_parser.h"
#include "hex_util.h"

#include <string.h>
#include <stdlib.h>

static void clear_event(esp_at_event_t *event)
{
    memset(event, 0, sizeof(*event));
    event->type = ESP_AT_EVT_UNKNOWN;
    event->desc_index = 0xFFu;
}

static uint32_t parse_number(const char *text)
{
    /* ESP-AT numeric fields are plain decimal; strtoul is bounded by the line. */
    return (uint32_t)strtoul(text, NULL, 10);
}

/* Split at most @p max_fields comma-separated fields in-place. */
static uint32_t split_fields(char *line, char **fields, uint32_t max_fields)
{
    uint32_t count = 0;
    char *p = line;

    if (max_fields == 0) {
        return 0;
    }
    fields[count++] = p;
    while (*p != '\0' && count < max_fields) {
        if (*p == ',') {
            *p = '\0';
            fields[count++] = p + 1;
        }
        ++p;
    }
    return count;
}

static bool starts_with(const char *line, const char *prefix)
{
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

static void decode_value(const char *hex, esp_at_event_t *event)
{
    size_t length = 0u;
    if (hex_decode(hex, event->data, sizeof(event->data), &length) != 0) {
        /* Too long or malformed: keep the decodable prefix and flag it. */
        event->data_truncated = true;
        size_t valid = 0u;
        while (hex[valid * 2u] != '\0' && valid * 2u < strlen(hex)) {
            if (valid >= sizeof(event->data)) {
                break;
            }
            char pair[3] = {hex[valid * 2u], hex[valid * 2u + 1u], '\0'};
            uint8_t value = 0u;
            size_t one = 0u;
            if (hex_decode(pair, &value, 1u, &one) != 0) {
                break;
            }
            event->data[valid++] = value;
        }
        length = valid;
    }
    event->data_length = (uint16_t)length;
}

static bool parse_write(const char *line, esp_at_event_t *event)
{
    char copy[320];
    char *fields[8];
    if (strlen(line) >= sizeof(copy)) {
        return false;
    }
    strcpy(copy, line + 7); /* skip "+WRITE:" */
    uint32_t count = split_fields(copy, fields, 8u);
    if (count != 5u && count != 6u) {
        return false;
    }

    if (count == 6u) {
        /* +WRITE:<conn>,<srv>,<char>,<desc>,<len>,<value> */
        event->conn_index = (uint8_t)parse_number(fields[0]);
        event->srv_index = (uint8_t)parse_number(fields[1]);
        event->char_index = (uint8_t)parse_number(fields[2]);
        event->desc_index = (uint8_t)parse_number(fields[3]);
        event->data_length = (uint16_t)parse_number(fields[4]);
        decode_value(fields[5], event);
    } else {
        /* +WRITE:<conn>,<srv>,<char>,<len>,<value> */
        event->conn_index = (uint8_t)parse_number(fields[0]);
        event->srv_index = (uint8_t)parse_number(fields[1]);
        event->char_index = (uint8_t)parse_number(fields[2]);
        event->data_length = (uint16_t)parse_number(fields[3]);
        decode_value(fields[4], event);
    }
    event->type = ESP_AT_EVT_BLE_WRITE;
    return true;
}

static bool parse_read(const char *line, esp_at_event_t *event)
{
    char copy[96];
    char *fields[6];
    if (strlen(line) >= sizeof(copy)) {
        return false;
    }
    strcpy(copy, line + 6); /* skip "+READ:" */
    uint32_t count = split_fields(copy, fields, 6u);
    if (count != 3u && count != 4u) {
        return false;
    }
    event->conn_index = (uint8_t)parse_number(fields[0]);
    event->srv_index = (uint8_t)parse_number(fields[1]);
    event->char_index = (uint8_t)parse_number(fields[2]);
    event->desc_index = (count == 4u) ? (uint8_t)parse_number(fields[3]) : 0xFFu;
    event->type = ESP_AT_EVT_BLE_READ;
    return true;
}

esp_at_event_type_t esp_at_parse_line(const char *line, esp_at_event_t *event)
{
    if (event == NULL) {
        return ESP_AT_EVT_UNKNOWN;
    }
    clear_event(event);
    if (line == NULL) {
        return ESP_AT_EVT_UNKNOWN;
    }

    while (*line == '\r' || *line == ' ') {
        ++line;
    }

    if (strcmp(line, "OK") == 0) {
        event->type = ESP_AT_EVT_OK;
    } else if (strcmp(line, "ERROR") == 0) {
        event->type = ESP_AT_EVT_ERROR;
    } else if (strcmp(line, "ready") == 0) {
        event->type = ESP_AT_EVT_READY;
    } else if (strcmp(line, ">") == 0) {
        event->type = ESP_AT_EVT_PROMPT;
    } else if (starts_with(line, "busy ")) {
        event->type = ESP_AT_EVT_BUSY;
    } else if (strcmp(line, "WIFI CONNECTED") == 0) {
        event->type = ESP_AT_EVT_WIFI_CONNECTED;
    } else if (strcmp(line, "WIFI GOT IP") == 0) {
        event->type = ESP_AT_EVT_WIFI_GOT_IP;
    } else if (strcmp(line, "WIFI DISCONNECT") == 0) {
        event->type = ESP_AT_EVT_WIFI_DISCONNECTED;
    } else if (starts_with(line, "+WRITE:")) {
        (void)parse_write(line, event);
    } else if (starts_with(line, "+READ:")) {
        (void)parse_read(line, event);
    } else if (starts_with(line, "+BLECONN:")) {
        event->conn_index = (uint8_t)parse_number(line + 9);
        event->type = ESP_AT_EVT_BLE_CONNECTED;
    } else if (starts_with(line, "+BLEDISCONN:")) {
        event->conn_index = (uint8_t)parse_number(line + 12);
        event->type = ESP_AT_EVT_BLE_DISCONNECTED;
    } else if (starts_with(line, "+CWJAP:")) {
        event->type = ESP_AT_EVT_CWJAP_RESULT;
    } else if (starts_with(line, "+CIPSTATUS:")) {
        event->type = ESP_AT_EVT_CIPSTATUS;
    }
    return event->type;
}
