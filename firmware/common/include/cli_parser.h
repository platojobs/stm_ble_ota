// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    cli_parser.h
 * @brief   Tolerant, host-testable parser for the serial diagnostic CLI.
 *
 * Recognised commands (case-insensitive, leading/trailing/repeated spaces
 * ignored):
 *   help
 *   version
 *   part
 *   wifi
 *   sensor
 *   ota start | ota status | ota abort
 *   factory reset
 *
 * Unknown or malformed input yields CLI_UNKNOWN (the caller prints usage).
 * Lines longer than CLI_MAX_LINE produce CLI_TOO_LONG.
 */

#ifndef CLI_PARSER_H
#define CLI_PARSER_H

#include <stdint.h>

#define CLI_MAX_LINE 128u

typedef enum {
    CLI_HELP = 0,
    CLI_VERSION,
    CLI_PART,
    CLI_WIFI,
    CLI_SENSOR,
    CLI_OTA_START,
    CLI_OTA_STATUS,
    CLI_OTA_ABORT,
    CLI_FACTORY_RESET,
    CLI_UNKNOWN,
    CLI_TOO_LONG,
} cli_command_t;

/** Parse a single line (no trailing newline required). */
cli_command_t cli_parse(const char *line, uint32_t length);

#endif /* CLI_PARSER_H */
