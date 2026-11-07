// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    ota_public_key.h
 * @brief   Reference Ed25519 public key compiled into the bootloader.
 *
 * This key pair ships with the reference project so the OTA example can be
 * exercised out of the box. It is an EXAMPLE key: it must never be used on a
 * real device, because its private half can be recreated or leaked by anyone.
 *
 * The matching private key lives only under tools/keys/ (git-ignored) and is
 * never committed; CI and release builds supply their private key from a
 * secret.
 *
 * Production procedure:
 *   1. generate a fresh keypair on an offline machine,
 *   2. replace this array with the public half and rebuild the bootloader,
 *   3. keep the private half outside the repository (CI secret / signing
 *      service), and rotate it by shipping a bootloader update.
 * See SECURITY.md for the consequences of a leaked signing key.
 */

#ifndef OTA_PUBLIC_KEY_H
#define OTA_PUBLIC_KEY_H

#include <stdint.h>

#define OTA_ED25519_PUBLIC_KEY_BYTES 32u

#define OTA_ED25519_PUBLIC_KEY {                                      \
    0x5D, 0x77, 0x6F, 0x1E, 0x3E, 0x8C, 0x4F, 0x16,                  \
    0x1A, 0xF4, 0xAF, 0x9E, 0x66, 0xE5, 0xD7, 0x40,                  \
    0xE3, 0xC0, 0x42, 0xFF, 0xD7, 0x58, 0xB0, 0xD6,                  \
    0x8D, 0x24, 0x56, 0xFE, 0x46, 0xC4, 0xFD, 0xBD                   \
}

#endif /* OTA_PUBLIC_KEY_H */
