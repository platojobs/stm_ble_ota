// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    device_info.h
 * @brief   Persisted manufacturing identity: serial number and hardware id.
 *
 * The firmware version is not stored here; it always comes from the running
 * image (APP_VERSION / OTA metadata). The device record lives in the two
 * alternating pages FLASH_DEVICE_PAGE0/1 and survives factory reset, because
 * a unit keeps its serial for life.
 */

#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include "record_store.h"

#include <stdint.h>
#include <stdbool.h>

#define DEVICE_SERIAL_MAX   24u
#define DEVICE_INFO_MAGIC   0x44494E46u /* "DINF" */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    char     serial[DEVICE_SERIAL_MAX + 1u];
    uint32_t hardware_id;
    uint8_t  provisioned;   /* serial was assigned (not a blank factory unit) */
    uint8_t  reserved[3];
} device_info_record_t;

typedef struct {
    char     serial[DEVICE_SERIAL_MAX + 1u];
    uint32_t hardware_id;
    bool     provisioned;
} device_info_t;

/** Blank identity: empty serial, provisioned = false. */
void device_info_defaults(device_info_t *info, uint32_t hardware_id);

/** @return true when hardware_id is in range and the serial is well formed. */
bool device_info_valid(const device_info_t *info);

/** Build "C562-XXXXXXXXXXXX" from the low 6 bytes of the MCU UID. */
void device_info_build_serial(const uint8_t uid[6],
                              char out[DEVICE_SERIAL_MAX + 1u]);

/** Load the persisted identity. Returns 0 on success, -1 when absent/invalid. */
int device_info_load(const record_store_t *store, device_info_t *out);

/** Persist the identity. Returns 0 on success. */
int device_info_save(record_store_t *store, const device_info_t *info);

#endif /* DEVICE_INFO_H */
