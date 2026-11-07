// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    ed25519_verify.h
 * @brief   Ed25519 signature verification with the key built into the image.
 *
 * Verification only: the device never sees the private key. The actual curve
 * arithmetic comes from the public-domain TweetNaCl implementation in
 * firmware/vendor/tweetnacl.
 */

#ifndef ED25519_VERIFY_H
#define ED25519_VERIFY_H

#include <stdint.h>
#include <stdbool.h>

#define ED25519_SIGNATURE_SIZE 64u
#define ED25519_PUBLIC_KEY_SIZE 32u

/** Verify @p signature over @p message with the compiled-in public key. */
bool ed25519_verify(const uint8_t signature[ED25519_SIGNATURE_SIZE],
                    const void *message, uint32_t message_len);

/**
 * Same operation with an explicit key. Used by host tests (RFC 8032 vectors)
 * and by tooling; the firmware path calls ed25519_verify().
 */
bool ed25519_verify_with_key(const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                             const uint8_t signature[ED25519_SIGNATURE_SIZE],
                             const void *message, uint32_t message_len);

#endif /* ED25519_VERIFY_H */
