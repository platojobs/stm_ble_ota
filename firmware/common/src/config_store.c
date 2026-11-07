// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "config_store.h"
#include "sha256.h"

#include <string.h>

/* XOR the buffer with SHA-256(key || block counter). Deterministic, which
 * keeps the record layout simple; the key must be device-specific. */
static void keystream_apply(const uint8_t key[WIFI_CONFIG_KEY_BYTES],
                            uint8_t *data, uint32_t length)
{
    uint8_t input[WIFI_CONFIG_KEY_BYTES + 1u];
    memcpy(input, key, WIFI_CONFIG_KEY_BYTES);

    for (uint32_t offset = 0, block = 0; offset < length; offset += 32u, ++block) {
        input[WIFI_CONFIG_KEY_BYTES] = (uint8_t)block;
        uint8_t stream[32];
        sha256(input, sizeof(input), stream);

        uint32_t chunk = length - offset;
        if (chunk > sizeof(stream)) {
            chunk = (uint32_t)sizeof(stream);
        }
        for (uint32_t i = 0; i < chunk; ++i) {
            data[offset + i] ^= stream[i];
        }
    }
}

static bool credential_bytes_ok(const char *value, size_t max_len)
{
    size_t len = strlen(value);
    if (len > max_len) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (c < 0x20u || c > 0x7Eu || c == '"' || c == '\\') {
            return false;
        }
    }
    return true;
}

bool wifi_credentials_valid(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) {
        return false;
    }
    return ssid[0] != '\0' &&
           credential_bytes_ok(ssid, WIFI_SSID_MAX) &&
           credential_bytes_ok(password, WIFI_PASSWORD_MAX);
}

bool wifi_config_valid(const wifi_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    /* Defensive: strings must be NUL-terminated inside their arrays. */
    if (memchr(config->ssid, '\0', sizeof(config->ssid)) == NULL) {
        return false;
    }
    if (memchr(config->password, '\0', sizeof(config->password)) == NULL) {
        return false;
    }
    return wifi_credentials_valid(config->ssid, config->password);
}

int wifi_config_load(const record_store_t *store, wifi_config_t *out,
                     const uint8_t key[WIFI_CONFIG_KEY_BYTES])
{
    wifi_config_record_t record;
    record_info_t info;
    char ssid[WIFI_SSID_MAX + 1u];
    char password[WIFI_PASSWORD_MAX + 1u];

    if (out == NULL || key == NULL) {
        return -1;
    }
    if (record_store_load(store, &record, sizeof(record), &info) != 0) {
        return -1;
    }
    if (record.magic != WIFI_CONFIG_MAGIC ||
        record.version != WIFI_CONFIG_RECORD_VERSION) {
        return -1;
    }
    if (record.ssid_len > WIFI_SSID_MAX || record.password_len > WIFI_PASSWORD_MAX) {
        return -1;
    }

    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
    memcpy(ssid, record.ssid, record.ssid_len);
    memcpy(password, record.password, record.password_len);
    keystream_apply(key, (uint8_t *)ssid, record.ssid_len);
    keystream_apply(key, (uint8_t *)password, record.password_len);

    memset(out, 0, sizeof(*out));
    memcpy(out->ssid, ssid, record.ssid_len);
    memcpy(out->password, password, record.password_len);
    out->auto_connect = record.auto_connect;
    out->provisioned = 1u;

    bool valid = wifi_config_valid(out);
    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
    if (!valid) {
        memset(out, 0, sizeof(*out));
        return -1;
    }
    return 0;
}

int wifi_config_save(record_store_t *store, const wifi_config_t *config,
                     const uint8_t key[WIFI_CONFIG_KEY_BYTES])
{
    wifi_config_record_t record;
    uint32_t sequence = 0u;

    if (!wifi_config_valid(config) || key == NULL) {
        return -1;
    }

    memset(&record, 0, sizeof(record));
    record.magic = WIFI_CONFIG_MAGIC;
    record.version = WIFI_CONFIG_RECORD_VERSION;
    record.ssid_len = (uint8_t)strlen(config->ssid);
    record.password_len = (uint8_t)strlen(config->password);
    record.auto_connect = config->auto_connect;
    memcpy(record.ssid, config->ssid, record.ssid_len);
    memcpy(record.password, config->password, record.password_len);
    keystream_apply(key, (uint8_t *)record.ssid, record.ssid_len);
    keystream_apply(key, (uint8_t *)record.password, record.password_len);

    return record_store_save(store, &record, sizeof(record), &sequence);
}

int wifi_config_clear(record_store_t *store)
{
    return record_store_clear(store);
}
