// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    config_store.h
 * @brief   Persisted WiFi provisioning data (SSID / password / auto-connect).
 *
 * The record is stored in the two-page config store defined in
 * flash_layout.h. SSID and password are obfuscated at rest with a SHA-256
 * keystream derived from a caller-supplied device key (see
 * wifi_config_save/load); the plaintext never reaches flash. This is
 * obfuscation, not a secret vault: the key is derived from the MCU UID and
 * anyone who can read the flash can also read the UID.
 */

#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include "record_store.h"

#include <stdint.h>
#include <stdbool.h>

#define WIFI_SSID_MAX     32u
#define WIFI_PASSWORD_MAX 64u
#define WIFI_CONFIG_MAGIC 0x57434647u /* "WCFG" */
#define WIFI_CONFIG_KEY_BYTES 32u
#define WIFI_CONFIG_RECORD_VERSION 2u

typedef struct {
    char ssid[WIFI_SSID_MAX + 1u];
    char password[WIFI_PASSWORD_MAX + 1u];
    uint8_t auto_connect;
    uint8_t provisioned;
} wifi_config_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint8_t  ssid_len;
    uint8_t  password_len;
    uint8_t  auto_connect;
    uint8_t  reserved[3];
    char     ssid[WIFI_SSID_MAX];      /* ciphertext */
    char     password[WIFI_PASSWORD_MAX]; /* ciphertext */
} wifi_config_record_t;

/** @return true when the in-memory config has a usable SSID. */
bool wifi_config_valid(const wifi_config_t *config);

/**
 * Validate externally supplied credentials before they reach flash or an
 * AT command: SSID 1..32 bytes, password 0..64 bytes, printable ASCII only,
 * and no '"' or '\\' (ESP-AT has no quoting for those inside CWJAP).
 */
bool wifi_credentials_valid(const char *ssid, const char *password);

/**
 * Load from the store and decode with @p key.
 * Returns 0 on success, -1 when absent/corrupt or the key does not decrypt.
 */
int wifi_config_load(const record_store_t *store, wifi_config_t *out,
                     const uint8_t key[WIFI_CONFIG_KEY_BYTES]);

/** Persist (obfuscating credentials with @p key). Returns 0 on success. */
int wifi_config_save(record_store_t *store, const wifi_config_t *config,
                     const uint8_t key[WIFI_CONFIG_KEY_BYTES]);

/** Erase both config pages. Returns 0 on success. */
int wifi_config_clear(record_store_t *store);

#endif /* CONFIG_STORE_H */
