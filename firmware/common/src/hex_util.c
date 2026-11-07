// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "hex_util.h"

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int hex_decode(const char *text, uint8_t *out, size_t out_capacity, size_t *out_length)
{
    size_t written = 0u;
    int high = -1;

    if (text == NULL || out == NULL) {
        return -1;
    }
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == ' ' || *p == ':' || *p == ',' || *p == '\r' || *p == '\n') {
            continue;
        }
        int v = hex_value(*p);
        if (v < 0) {
            return -1;
        }
        if (high < 0) {
            high = v;
        } else {
            if (written >= out_capacity) {
                return -1;
            }
            out[written++] = (uint8_t)((high << 4) | v);
            high = -1;
        }
    }
    if (high >= 0) {
        return -1; /* odd number of hex digits */
    }
    if (out_length != NULL) {
        *out_length = written;
    }
    return 0;
}

int hex_encode(const uint8_t *data, size_t length, char *out, size_t out_capacity)
{
    size_t written = 0u;
    if (data == NULL || out == NULL) {
        return -1;
    }
    for (size_t i = 0; i < length; ++i) {
        if (written + 3u > out_capacity) {
            return -1;
        }
        uint8_t v = data[i];
        out[written++] = (char)((v >> 4u) < 10u ? ('0' + (v >> 4u)) : ('a' + (v >> 4u) - 10u));
        out[written++] = (char)((v & 0x0Fu) < 10u ? ('0' + (v & 0x0Fu))
                                                  : ('a' + (v & 0x0Fu) - 10u));
    }
    out[written] = '\0';
    return (int)written;
}
