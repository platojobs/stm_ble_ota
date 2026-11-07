// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    cli_service.h
 * @brief   Execution of the serial diagnostic CLI commands.
 */

#ifndef CLI_SERVICE_H
#define CLI_SERVICE_H

#include <stdint.h>

/** Execute one parsed or raw CLI line and emit JSON responses on USART2. */
void cli_service_handle_line(const char *line, uint32_t length);

#endif /* CLI_SERVICE_H */
