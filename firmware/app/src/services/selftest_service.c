// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "selftest_service.h"
#include "device_service.h"
#include "ble_service.h"
#include "sht30.h"
#include "bsp_adc.h"
#include "fault_service.h"

#include <string.h>

void selftest_service_run(selftest_result_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    out->flash_ok = device_service_ready();

    int32_t temperature = 0;
    int32_t humidity = 0;
    out->i2c_ok = sht30_read(&temperature, &humidity) == 0;

    uint16_t millivolts = 0;
    out->adc_ok = bsp_adc_read_mv(&millivolts) == 0;

    out->ble_ok = ble_service_ready();

    out->failed_mask = 0u;
    if (!out->flash_ok) {
        out->failed_mask |= SELFTEST_FLASH_BIT;
    }
    if (!out->i2c_ok) {
        out->failed_mask |= SELFTEST_I2C_BIT;
    }
    if (!out->adc_ok) {
        out->failed_mask |= SELFTEST_ADC_BIT;
    }
    if (!out->ble_ok) {
        out->failed_mask |= SELFTEST_BLE_BIT;
    }
    out->all_ok = out->failed_mask == 0u;

    if (!out->all_ok) {
        fault_service_raise(ERR_CODE_SELFTEST_FAILED, out->failed_mask);
    }
}
