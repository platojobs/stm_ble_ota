// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "device_info.h"

#include <stdio.h>
#include <string.h>

static bool serial_chars_ok(const char *serial, size_t length)
{
    if (length == 0u || length > DEVICE_SERIAL_MAX) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)serial[i];
        bool ok = (c >= '0' && c <= '9') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') ||
                  c == '-' || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

void device_info_defaults(device_info_t *info, uint32_t hardware_id)
{
    if (info == NULL) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->hardware_id = hardware_id;
    info->provisioned = false;
}

bool device_info_valid(const device_info_t *info)
{
    if (info == NULL || info->hardware_id == 0u) {
        return false;
    }
    if (memchr(info->serial, '\0', sizeof(info->serial)) == NULL) {
        return false;
    }
    return serial_chars_ok(info->serial, strlen(info->serial));
}

void device_info_build_serial(const uint8_t uid[6],
                              char out[DEVICE_SERIAL_MAX + 1u])
{
    if (out == NULL) {
        return;
    }
    if (uid == NULL) {
        out[0] = '\0';
        return;
    }
    (void)snprintf(out, DEVICE_SERIAL_MAX + 1u, "C562-%02X%02X%02X%02X%02X%02X",
                   (unsigned)uid[0], (unsigned)uid[1], (unsigned)uid[2],
                   (unsigned)uid[3], (unsigned)uid[4], (unsigned)uid[5]);
}

int device_info_load(const record_store_t *store, device_info_t *out)
{
    device_info_record_t record;

    if (out == NULL) {
        return -1;
    }
    if (record_store_load(store, &record, sizeof(record), NULL) != 0) {
        return -1;
    }
    if (record.magic != DEVICE_INFO_MAGIC) {
        return -1;
    }

    device_info_t info;
    memset(&info, 0, sizeof(info));
    if (memchr(record.serial, '\0', sizeof(record.serial)) == NULL) {
        return -1;
    }
    size_t length = strlen(record.serial);
    if (length > DEVICE_SERIAL_MAX) {
        return -1;
    }
    memcpy(info.serial, record.serial, length);
    info.serial[length] = '\0';
    info.hardware_id = record.hardware_id;
    info.provisioned = record.provisioned != 0u;

    if (!device_info_valid(&info)) {
        return -1;
    }
    *out = info;
    return 0;
}

int device_info_save(record_store_t *store, const device_info_t *info)
{
    device_info_record_t record;

    if (!device_info_valid(info)) {
        return -1;
    }

    memset(&record, 0, sizeof(record));
    record.magic = DEVICE_INFO_MAGIC;
    memcpy(record.serial, info->serial, strlen(info->serial));
    record.hardware_id = info->hardware_id;
    record.provisioned = info->provisioned ? 1u : 0u;

    return record_store_save(store, &record, sizeof(record), NULL);
}
