// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "ed25519_verify.h"
#include "ota_public_key.h"

#include "tweetnacl.h"

#include <string.h>

#define MAX_SIGNED_BYTES 256u

/* TweetNaCl declares this in tweetnacl.c. The firmware only verifies, so no
 * random number source is needed; the stub keeps the unused keypair code
 * linkable and makes accidental use deterministic rather than undefined. */
void randombytes(unsigned char *buffer, unsigned long long length)
{
    (void)buffer;
    (void)length;
}

bool ed25519_verify_with_key(const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE],
                             const uint8_t signature[ED25519_SIGNATURE_SIZE],
                             const void *message, uint32_t message_len)
{
    uint8_t signed_message[MAX_SIGNED_BYTES + ED25519_SIGNATURE_SIZE];
    uint8_t opened[MAX_SIGNED_BYTES + ED25519_SIGNATURE_SIZE];
    unsigned long long opened_len = 0u;

    if (public_key == NULL || signature == NULL ||
        (message == NULL && message_len != 0u) ||
        message_len > MAX_SIGNED_BYTES) {
        return false;
    }

    memcpy(signed_message, signature, ED25519_SIGNATURE_SIZE);
    if (message_len > 0u) {
        memcpy(signed_message + ED25519_SIGNATURE_SIZE, message, message_len);
    }

    if (crypto_sign_open(opened, &opened_len, signed_message,
                         (unsigned long long)ED25519_SIGNATURE_SIZE + message_len,
                         public_key) != 0) {
        return false;
    }
    return opened_len == message_len;
}

bool ed25519_verify(const uint8_t signature[ED25519_SIGNATURE_SIZE],
                    const void *message, uint32_t message_len)
{
    static const uint8_t public_key[ED25519_PUBLIC_KEY_SIZE] =
        OTA_ED25519_PUBLIC_KEY;
    return ed25519_verify_with_key(public_key, signature, message, message_len);
}
