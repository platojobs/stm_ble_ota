// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    selftest_service.h
 * @brief   Production-line self-test: flash, I2C sensor, ADC and BLE.
 *
 * The test is non-destructive (no erase/program) and reports per-item
 * pass/fail plus a bitmask so the line fixture can tell which block failed.
 */

#ifndef SELFTEST_SERVICE_H
#define SELFTEST_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

#define SELFTEST_FLASH_BIT 0x01u
#define SELFTEST_I2C_BIT   0x02u
#define SELFTEST_ADC_BIT   0x04u
#define SELFTEST_BLE_BIT   0x08u

/* Extra checks run by the one-shot factory flow. */
#define FACTORY_CHECK_SERIAL_BIT 0x10u
#define FACTORY_CHECK_CRASH_BIT  0x20u

typedef struct {
    bool flash_ok;
    bool i2c_ok;
    bool adc_ok;
    bool ble_ok;
    bool all_ok;
    uint8_t failed_mask;
} selftest_result_t;

void selftest_service_run(selftest_result_t *out);

/** One-shot production check: selftest plus identity and crash history. */
typedef struct {
    selftest_result_t selftest;
    bool serial_ok;
    bool crash_clean;
    bool all_ok;
    uint8_t failed_mask;
} factory_result_t;

void factory_service_run(factory_result_t *out);

#endif /* SELFTEST_SERVICE_H */
