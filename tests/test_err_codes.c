// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "test_framework.h"
#include "err_code.h"

#include <string.h>

static const err_code_t k_all_codes[] = {
    ERR_CODE_OK,

    ERR_CODE_INVALID_ARG, ERR_CODE_NULL_POINTER, ERR_CODE_BUFFER_TOO_SMALL,
    ERR_CODE_MALFORMED_FRAME, ERR_CODE_BAD_CRC, ERR_CODE_UNSUPPORTED,
    ERR_CODE_LINE_TOO_LONG,

    ERR_CODE_FLASH_ERASE, ERR_CODE_FLASH_PROGRAM, ERR_CODE_FLASH_READ,
    ERR_CODE_STORE_EMPTY, ERR_CODE_STORE_CORRUPT, ERR_CODE_STORE_FULL,

    ERR_CODE_OTA_BAD_STATE, ERR_CODE_OTA_BAD_SIZE, ERR_CODE_OTA_BAD_CRC,
    ERR_CODE_OTA_OUT_OF_ORDER, ERR_CODE_OTA_TIMEOUT, ERR_CODE_OTA_ABORTED,
    ERR_CODE_OTA_BAD_VERSION,

    ERR_CODE_IMAGE_SIZE, ERR_CODE_IMAGE_VECTOR, ERR_CODE_IMAGE_CRC,
    ERR_CODE_IMAGE_SHA256, ERR_CODE_IMAGE_SIGNATURE,

    ERR_CODE_AT_TIMEOUT, ERR_CODE_AT_ERROR, ERR_CODE_WIFI_NOT_PROVISIONED,
    ERR_CODE_WIFI_CONNECT_FAILED, ERR_CODE_BLE_UNAVAILABLE,

    ERR_CODE_I2C_ERROR, ERR_CODE_SENSOR_BUSY, ERR_CODE_SENSOR_RANGE,

    ERR_CODE_WATCHDOG_RESET, ERR_CODE_BOOT_FAILED, ERR_CODE_ROLLBACK,
    ERR_CODE_NOT_IMPLEMENTED, ERR_CODE_SELFTEST_FAILED,
};

#define ALL_CODE_COUNT (sizeof(k_all_codes) / sizeof(k_all_codes[0]))

TEST_CASE(err_code_table_is_complete_and_unique)
{
    for (size_t i = 0; i < ALL_CODE_COUNT; ++i) {
        ASSERT_TRUE(err_code_is_known(k_all_codes[i]));
        ASSERT_STREQ(err_code_name(k_all_codes[i]), err_code_name(k_all_codes[i]));
        ASSERT_TRUE(strcmp(err_code_domain_name(k_all_codes[i]), "unknown") != 0);

        for (size_t j = i + 1; j < ALL_CODE_COUNT; ++j) {
            ASSERT_TRUE(k_all_codes[i] != k_all_codes[j]);
        }
    }
}

TEST_CASE(err_code_ok_and_unknown_names)
{
    ASSERT_STREQ(err_code_name(ERR_CODE_OK), "OK");
    ASSERT_STREQ(err_code_domain_name(ERR_CODE_OK), "ok");
    ASSERT_TRUE(err_code_is_known(ERR_CODE_OK));

    err_code_t bogus = (err_code_t)0xFFFF;
    ASSERT_STREQ(err_code_name(bogus), "UNKNOWN");
    ASSERT_STREQ(err_code_domain_name(bogus), "unknown");
    ASSERT_FALSE(err_code_is_known(bogus));
}

TEST_CASE(err_code_domains_follow_high_byte)
{
    ASSERT_STREQ(err_code_domain_name(ERR_CODE_INVALID_ARG), "arg");
    ASSERT_STREQ(err_code_domain_name(ERR_CODE_FLASH_PROGRAM), "flash");
    ASSERT_STREQ(err_code_domain_name(ERR_CODE_OTA_BAD_SIZE), "ota");
    ASSERT_STREQ(err_code_domain_name(ERR_CODE_IMAGE_SIGNATURE), "image");
    ASSERT_STREQ(err_code_domain_name(ERR_CODE_AT_TIMEOUT), "network");
    ASSERT_STREQ(err_code_domain_name(ERR_CODE_I2C_ERROR), "sensor");
    ASSERT_STREQ(err_code_domain_name(ERR_CODE_BOOT_FAILED), "system");
}

TEST_CASE(err_code_maps_every_ota_reason)
{
    ASSERT_EQ(err_code_from_ota_reason(OTA_REASON_NONE), ERR_CODE_OK);
    ASSERT_EQ(err_code_from_ota_reason(OTA_REASON_BAD_STATE),
              ERR_CODE_OTA_BAD_STATE);
    ASSERT_EQ(err_code_from_ota_reason(OTA_REASON_BAD_SIZE),
              ERR_CODE_OTA_BAD_SIZE);
    ASSERT_EQ(err_code_from_ota_reason(OTA_REASON_BAD_CRC),
              ERR_CODE_OTA_BAD_CRC);
    ASSERT_EQ(err_code_from_ota_reason(OTA_REASON_OUT_OF_ORDER),
              ERR_CODE_OTA_OUT_OF_ORDER);
    ASSERT_EQ(err_code_from_ota_reason(OTA_REASON_FLASH_ERROR),
              ERR_CODE_FLASH_PROGRAM);
    ASSERT_EQ(err_code_from_ota_reason(OTA_REASON_TIMEOUT),
              ERR_CODE_OTA_TIMEOUT);
    ASSERT_EQ(err_code_from_ota_reason(OTA_REASON_ABORTED),
              ERR_CODE_OTA_ABORTED);
    ASSERT_EQ(err_code_from_ota_reason(OTA_REASON_BAD_VERSION),
              ERR_CODE_OTA_BAD_VERSION);
    ASSERT_EQ(err_code_from_ota_reason(OTA_REASON_BAD_SIGNATURE),
              ERR_CODE_IMAGE_SIGNATURE);
    ASSERT_EQ(err_code_from_ota_reason((ota_reason_t)0x7F), ERR_CODE_INVALID_ARG);
}

TEST_CASE(err_code_maps_every_image_result)
{
    ASSERT_EQ(err_code_from_image_result(IMAGE_OK), ERR_CODE_OK);
    ASSERT_EQ(err_code_from_image_result(IMAGE_ERR_SIZE), ERR_CODE_IMAGE_SIZE);
    ASSERT_EQ(err_code_from_image_result(IMAGE_ERR_VECTOR), ERR_CODE_IMAGE_VECTOR);
    ASSERT_EQ(err_code_from_image_result(IMAGE_ERR_CRC), ERR_CODE_IMAGE_CRC);
    ASSERT_EQ(err_code_from_image_result(IMAGE_ERR_FLASH), ERR_CODE_FLASH_READ);
    ASSERT_EQ(err_code_from_image_result(IMAGE_ERR_SHA256), ERR_CODE_IMAGE_SHA256);
    ASSERT_EQ(err_code_from_image_result(IMAGE_ERR_SIGNATURE),
              ERR_CODE_IMAGE_SIGNATURE);
    ASSERT_EQ(err_code_from_image_result((image_check_result_t)0x7F),
              ERR_CODE_INVALID_ARG);
}

void run_err_code_tests(void)
{
    RUN_TEST(err_code_table_is_complete_and_unique);
    RUN_TEST(err_code_ok_and_unknown_names);
    RUN_TEST(err_code_domains_follow_high_byte);
    RUN_TEST(err_code_maps_every_ota_reason);
    RUN_TEST(err_code_maps_every_image_result);
}
