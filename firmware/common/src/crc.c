// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "crc.h"

uint32_t crc32_ieee(const void *data, size_t len, uint32_t seed)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = seed;

    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

uint32_t crc32_ieee_buf(const void *data, size_t len)
{
    return crc32_ieee(data, len, 0xFFFFFFFFu) ^ 0xFFFFFFFFu;
}

uint16_t crc16_ccitt(const void *data, size_t len, uint16_t seed)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = seed;

    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)p[i] << 8u;
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1u) ^ 0x1021u)
                                  : (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

uint16_t crc16_ccitt_buf(const void *data, size_t len)
{
    return crc16_ccitt(data, len, 0xFFFFu);
}

uint8_t crc8_sensirion(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFFu;

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            if ((crc & 0x80u) != 0u) {
                crc = (uint8_t)((crc << 1u) ^ 0x31u);
            } else {
                crc = (uint8_t)(crc << 1u);
            }
        }
    }
    return crc;
}
