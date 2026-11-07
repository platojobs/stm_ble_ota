// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "cli_parser.h"

#include <string.h>
#include <stdbool.h>

static char to_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

/* Copy @p length bytes from @p line into @p out as lowercase, skipping leading
 * and repeated internal spaces. Returns the length of the normalised token
 * stream (words separated by single spaces). */
static uint32_t normalise(const char *line, uint32_t length, char *out,
                          uint32_t out_size)
{
    uint32_t o = 0u;
    bool in_word = false;
    for (uint32_t i = 0u; i < length; ++i) {
        char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (in_word) {
                if (o + 1u < out_size) {
                    out[o++] = ' ';
                }
                in_word = false;
            }
            continue;
        }
        if (o + 1u >= out_size) {
            return 0u; /* would not fit */
        }
        out[o++] = to_lower(c);
        in_word = true;
    }
    if (in_word && o > 0u && out[o - 1u] == ' ') {
        --o;
    }
    while (o > 0u && out[o - 1u] == ' ') {
        --o;
    }
    out[o] = '\0';
    return o;
}

cli_command_t cli_parse(const char *line, uint32_t length)
{
    if (line == NULL) {
        return CLI_UNKNOWN;
    }
    if (length > CLI_MAX_LINE) {
        return CLI_TOO_LONG;
    }

    char buf[CLI_MAX_LINE + 1u];
    if (normalise(line, length, buf, sizeof(buf)) == 0u) {
        return CLI_UNKNOWN;
    }

    if (strcmp(buf, "help") == 0) {
        return CLI_HELP;
    }
    if (strcmp(buf, "version") == 0) {
        return CLI_VERSION;
    }
    if (strcmp(buf, "part") == 0 || strcmp(buf, "partition") == 0) {
        return CLI_PART;
    }
    if (strcmp(buf, "wifi") == 0 || strcmp(buf, "wifi status") == 0) {
        return CLI_WIFI;
    }
    if (strcmp(buf, "sensor") == 0 || strcmp(buf, "sensor read") == 0) {
        return CLI_SENSOR;
    }
    if (strcmp(buf, "ota start") == 0) {
        return CLI_OTA_START;
    }
    if (strcmp(buf, "ota status") == 0) {
        return CLI_OTA_STATUS;
    }
    if (strcmp(buf, "ota abort") == 0) {
        return CLI_OTA_ABORT;
    }
    if (strcmp(buf, "log") == 0 || strcmp(buf, "faults") == 0) {
        return CLI_LOG;
    }
    if (strcmp(buf, "device") == 0 || strcmp(buf, "info") == 0) {
        return CLI_DEVICE;
    }
    if (strcmp(buf, "selftest") == 0 || strcmp(buf, "self-test") == 0 ||
        strcmp(buf, "test") == 0) {
        return CLI_SELFTEST;
    }
    if (strcmp(buf, "factory reset") == 0 || strcmp(buf, "factory") == 0) {
        return CLI_FACTORY_RESET;
    }
    return CLI_UNKNOWN;
}
