// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "config_store.h"

#include <string.h>

bool wifi_config_valid(const wifi_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    if (config->ssid[0] == '\0') {
        return false;
    }
    /* Defensive: strings must be NUL-terminated inside their arrays. */
    if (memchr(config->ssid, '\0', sizeof(config->ssid)) == NULL) {
        return false;
    }
    if (memchr(config->password, '\0', sizeof(config->password)) == NULL) {
        return false;
    }
    return true;
}

int wifi_config_load(const record_store_t *store, wifi_config_t *out)
{
    wifi_config_record_t record;
    record_info_t info;

    if (out == NULL) {
        return -1;
    }
    if (record_store_load(store, &record, sizeof(record), &info) != 0) {
        return -1;
    }
    if (record.magic != WIFI_CONFIG_MAGIC) {
        return -1;
    }
    if (record.ssid_len > WIFI_SSID_MAX || record.password_len > WIFI_PASSWORD_MAX) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    memcpy(out->ssid, record.ssid, record.ssid_len);
    memcpy(out->password, record.password, record.password_len);
    out->ssid[record.ssid_len] = '\0';
    out->password[record.password_len] = '\0';
    out->auto_connect = record.auto_connect;
    out->provisioned = 1u;
    return wifi_config_valid(out) ? 0 : -1;
}

int wifi_config_save(record_store_t *store, const wifi_config_t *config)
{
    wifi_config_record_t record;
    uint32_t sequence = 0u;

    if (!wifi_config_valid(config)) {
        return -1;
    }

    memset(&record, 0, sizeof(record));
    record.magic = WIFI_CONFIG_MAGIC;
    record.ssid_len = (uint8_t)strlen(config->ssid);
    record.password_len = (uint8_t)strlen(config->password);
    record.auto_connect = config->auto_connect;
    memcpy(record.ssid, config->ssid, record.ssid_len);
    memcpy(record.password, config->password, record.password_len);

    return record_store_save(store, &record, sizeof(record), &sequence);
}

int wifi_config_clear(record_store_t *store)
{
    return record_store_clear(store);
}
