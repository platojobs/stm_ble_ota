// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "test_framework.h"
#include "device_info.h"
#include "cli_parser.h"

#include <string.h>

TEST_CASE(device_info_normalizes_factory_serial)
{
    char out[DEVICE_SERIAL_MAX + 1u];

    ASSERT_TRUE(device_info_normalize_serial("c562-ab12_9", out));
    ASSERT_EQ(strcmp(out, "C562-AB12_9"), 0);

    ASSERT_FALSE(device_info_normalize_serial("", out));
    ASSERT_FALSE(device_info_normalize_serial("bad serial", out));
    ASSERT_FALSE(device_info_normalize_serial("bad/serial", out));
    ASSERT_FALSE(device_info_normalize_serial("0123456789012345678901234", out));
}

/* The serial round-trip through the record store is already covered by
 * device_info_validation_and_roundtrip in test_ota_logic.c. */

TEST_CASE(cli_parses_serial_and_factory_commands)
{
    const char *serial = "serial";
    const char *show = "SERIAL SHOW";
    const char *set = "serial set c562-abc";
    const char *no_arg = "serial set ";
    const char *run = "factory test";
    const char *run_alias = "factory run";
    const char *reset = "factory reset";

    ASSERT_EQ(cli_parse(serial, (uint32_t)strlen(serial)), CLI_SERIAL);
    ASSERT_EQ(cli_parse(show, (uint32_t)strlen(show)), CLI_SERIAL);
    ASSERT_EQ(cli_parse(set, (uint32_t)strlen(set)), CLI_SERIAL_SET);
    ASSERT_EQ(cli_parse(no_arg, (uint32_t)strlen(no_arg)), CLI_UNKNOWN);
    ASSERT_EQ(cli_parse(run, (uint32_t)strlen(run)), CLI_FACTORY_TEST);
    ASSERT_EQ(cli_parse(run_alias, (uint32_t)strlen(run_alias)), CLI_FACTORY_TEST);
    ASSERT_EQ(cli_parse(reset, (uint32_t)strlen(reset)), CLI_FACTORY_RESET);
}

void run_factory_tests(void)
{
    RUN_TEST(device_info_normalizes_factory_serial);
    RUN_TEST(cli_parses_serial_and_factory_commands);
}
