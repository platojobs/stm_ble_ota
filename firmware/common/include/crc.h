/**
 * @file    crc.h
 * @brief   CRC helpers used by the OTA link and the image integrity checks.
 */

#ifndef CRC_H
#define CRC_H

#include <stdint.h>
#include <stddef.h>

/** CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320, init 0xFFFFFFFF, xorout). */
uint32_t crc32_ieee(const void *data, size_t len, uint32_t seed);

/** Convenience wrapper with the standard initial value. */
uint32_t crc32_ieee_buf(const void *data, size_t len);

/** CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout). */
uint16_t crc16_ccitt(const void *data, size_t len, uint16_t seed);
uint16_t crc16_ccitt_buf(const void *data, size_t len);

/** CRC-8 used by Sensirion SHT3x (poly 0x31, init 0xFF). */
uint8_t crc8_sensirion(const uint8_t *data, size_t len);

#endif /* CRC_H */
