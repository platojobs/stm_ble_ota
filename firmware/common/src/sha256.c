// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "sha256.h"

#include <string.h>

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32u - (n))))

static const uint32_t K[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u,
};

static void compress(sha256_t *ctx, const uint8_t block[SHA256_BLOCK_SIZE])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;

    for (uint32_t i = 0; i < 16u; ++i) {
        w[i] = ((uint32_t)block[i * 4u] << 24u) |
               ((uint32_t)block[i * 4u + 1u] << 16u) |
               ((uint32_t)block[i * 4u + 2u] << 8u) |
               (uint32_t)block[i * 4u + 3u];
    }
    for (uint32_t i = 16u; i < 64u; ++i) {
        uint32_t s0 = ROTR(w[i - 15u], 7u) ^ ROTR(w[i - 15u], 18u) ^
                      (w[i - 15u] >> 3u);
        uint32_t s1 = ROTR(w[i - 2u], 17u) ^ ROTR(w[i - 2u], 19u) ^
                      (w[i - 2u] >> 10u);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (uint32_t i = 0; i < 64u; ++i) {
        uint32_t s1 = ROTR(e, 6u) ^ ROTR(e, 11u) ^ ROTR(e, 25u);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + K[i] + w[i];
        uint32_t s0 = ROTR(a, 2u) ^ ROTR(a, 13u) ^ ROTR(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(sha256_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x6A09E667u;
    ctx->state[1] = 0xBB67AE85u;
    ctx->state[2] = 0x3C6EF372u;
    ctx->state[3] = 0xA54FF53Au;
    ctx->state[4] = 0x510E527Fu;
    ctx->state[5] = 0x9B05688Cu;
    ctx->state[6] = 0x1F83D9ABu;
    ctx->state[7] = 0x5BE0CD19u;
}

void sha256_update(sha256_t *ctx, const void *data, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    ctx->total += length;

    while (length > 0u) {
        uint32_t space = SHA256_BLOCK_SIZE - ctx->block_len;
        uint32_t take = (length < space) ? length : space;
        memcpy(ctx->block + ctx->block_len, bytes, take);
        ctx->block_len += take;
        bytes += take;
        length -= take;
        if (ctx->block_len == SHA256_BLOCK_SIZE) {
            compress(ctx, ctx->block);
            ctx->block_len = 0u;
        }
    }
}

void sha256_final(sha256_t *ctx, uint8_t out[SHA256_DIGEST_SIZE])
{
    uint64_t bits = ctx->total * 8u;
    uint32_t index = ctx->block_len;

    ctx->block[index++] = 0x80u;
    if (index > 56u) {
        memset(ctx->block + index, 0, SHA256_BLOCK_SIZE - index);
        compress(ctx, ctx->block);
        index = 0u;
    }
    memset(ctx->block + index, 0, 56u - index);
    for (uint32_t i = 0; i < 8u; ++i) {
        ctx->block[63u - i] = (uint8_t)(bits & 0xFFu);
        bits >>= 8u;
    }
    compress(ctx, ctx->block);

    for (uint32_t i = 0; i < 8u; ++i) {
        out[i * 4u] = (uint8_t)(ctx->state[i] >> 24u);
        out[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 16u);
        out[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 8u);
        out[i * 4u + 3u] = (uint8_t)ctx->state[i];
    }
}

void sha256(const void *data, uint32_t length, uint8_t out[SHA256_DIGEST_SIZE])
{
    sha256_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, length);
    sha256_final(&ctx, out);
}
