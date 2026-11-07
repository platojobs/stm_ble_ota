// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    sha256.h
 * @brief   Streaming SHA-256 (FIPS 180-4), used for image digests and the
 *          Ed25519-signed descriptor.
 *
 * The implementation is self-contained so the bootloader can hash flash while
 * reading it without pulling in a general-purpose crypto library.
 */

#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_DIGEST_SIZE 32u
#define SHA256_BLOCK_SIZE  64u

typedef struct {
    uint32_t state[8];
    uint64_t total;
    uint8_t  block[SHA256_BLOCK_SIZE];
    uint32_t block_len;
} sha256_t;

void sha256_init(sha256_t *ctx);

/** Feed bytes; @p length is in bytes (images are far below 4 GiB). */
void sha256_update(sha256_t *ctx, const void *data, uint32_t length);

/** Finish and write the 32-byte digest. The context must not be reused. */
void sha256_final(sha256_t *ctx, uint8_t out[SHA256_DIGEST_SIZE]);

/** One-shot helper. */
void sha256(const void *data, uint32_t length, uint8_t out[SHA256_DIGEST_SIZE]);

#endif /* SHA256_H */
