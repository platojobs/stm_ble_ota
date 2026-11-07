// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "test_framework.h"
#include "error_log.h"

#include <string.h>

TEST_CASE(error_log_ignores_success_and_null)
{
    error_log_t log;
    error_log_init(&log);

    ASSERT_FALSE(error_log_push(&log, ERR_CODE_OK, 0u, 10u));
    ASSERT_FALSE(error_log_push(NULL, ERR_CODE_I2C_ERROR, 0u, 10u));
    ASSERT_EQ(error_log_count(&log), 0u);
    ASSERT_EQ(error_log_total(&log), 0u);
    ASSERT_EQ(error_log_last_code(&log), ERR_CODE_OK);

    ASSERT_TRUE(error_log_push(&log, ERR_CODE_I2C_ERROR, 0u, 10u));
    ASSERT_EQ(error_log_count(&log), 1u);
    ASSERT_EQ(error_log_last_code(&log), ERR_CODE_I2C_ERROR);
}

TEST_CASE(error_log_wraps_and_counts_dropped)
{
    error_log_t log;
    error_log_init(&log);

    for (uint16_t i = 0; i < ERROR_LOG_CAPACITY + 2u; ++i) {
        ASSERT_TRUE(error_log_push(&log, ERR_CODE_OTA_TIMEOUT, i, i * 100u));
    }

    ASSERT_EQ(error_log_count(&log), ERROR_LOG_CAPACITY);
    ASSERT_EQ(error_log_total(&log), ERROR_LOG_CAPACITY + 2u);
    ASSERT_EQ(error_log_dropped(&log), 2u);

    /* Oldest retained entry is the third push (detail 2). */
    error_log_entry_t first;
    ASSERT_TRUE(error_log_get(&log, 0u, &first));
    ASSERT_EQ(first.detail, 2u);
    ASSERT_EQ(first.code, (uint16_t)ERR_CODE_OTA_TIMEOUT);

    error_log_entry_t last;
    ASSERT_TRUE(error_log_get(&log, ERROR_LOG_CAPACITY - 1u, &last));
    ASSERT_EQ(last.detail, ERROR_LOG_CAPACITY + 1u);
    ASSERT_EQ(error_log_last_code(&log), ERR_CODE_OTA_TIMEOUT);

    ASSERT_FALSE(error_log_get(&log, ERROR_LOG_CAPACITY, &last));
    ASSERT_FALSE(error_log_get(&log, 0u, NULL));
}

TEST_CASE(error_log_keeps_entries_in_age_order)
{
    error_log_t log;
    error_log_init(&log);

    ASSERT_TRUE(error_log_push(&log, ERR_CODE_FLASH_PROGRAM, 7u, 1000u));
    ASSERT_TRUE(error_log_push(&log, ERR_CODE_IMAGE_SIGNATURE, 9u, 2000u));

    error_log_entry_t entry;
    ASSERT_TRUE(error_log_get(&log, 0u, &entry));
    ASSERT_EQ(entry.code, (uint16_t)ERR_CODE_FLASH_PROGRAM);
    ASSERT_EQ(entry.timestamp_ms, 1000u);
    ASSERT_EQ(entry.detail, 7u);

    ASSERT_TRUE(error_log_get(&log, 1u, &entry));
    ASSERT_EQ(entry.code, (uint16_t)ERR_CODE_IMAGE_SIGNATURE);
    ASSERT_EQ(entry.timestamp_ms, 2000u);
    ASSERT_EQ(entry.detail, 9u);
}

TEST_CASE(error_log_json_reports_entries)
{
    error_log_t log;
    error_log_init(&log);

    ASSERT_TRUE(error_log_push(&log, ERR_CODE_WIFI_NOT_PROVISIONED, 0u, 1000u));
    ASSERT_TRUE(error_log_push(&log, ERR_CODE_I2C_ERROR, 0u, 1500u));

    char json[600];
    uint32_t length = error_log_to_json(&log, json, sizeof(json));
    ASSERT_TRUE(length > 0u);
    ASSERT_EQ((uint32_t)strlen(json), length);

    ASSERT_TRUE(strstr(json, "\"type\":\"fault\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"count\":2") != NULL);
    ASSERT_TRUE(strstr(json, "\"total\":2") != NULL);
    ASSERT_TRUE(strstr(json, "\"dropped\":0") != NULL);
    ASSERT_TRUE(strstr(json, "\"code\":1283") != NULL); /* 0x0503 */
    ASSERT_TRUE(strstr(json, "\"name\":\"WIFI_NOT_PROVISIONED\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"name\":\"I2C_ERROR\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"t_ms\":1500") != NULL);
    ASSERT_TRUE(strstr(json, "\"detail\":0") != NULL);
}

TEST_CASE(error_log_json_rejects_small_buffer)
{
    error_log_t log;
    error_log_init(&log);
    ASSERT_TRUE(error_log_push(&log, ERR_CODE_I2C_ERROR, 0u, 1000u));

    char tiny[8];
    ASSERT_EQ(error_log_to_json(&log, tiny, sizeof(tiny)), 0u);
    ASSERT_EQ(tiny[0], '\0');
    ASSERT_EQ(error_log_to_json(&log, NULL, sizeof(tiny)), 0u);
    ASSERT_EQ(error_log_to_json(NULL, tiny, sizeof(tiny)), 0u);
}

void run_error_log_tests(void)
{
    RUN_TEST(error_log_ignores_success_and_null);
    RUN_TEST(error_log_wraps_and_counts_dropped);
    RUN_TEST(error_log_keeps_entries_in_age_order);
    RUN_TEST(error_log_json_reports_entries);
    RUN_TEST(error_log_json_rejects_small_buffer);
}
