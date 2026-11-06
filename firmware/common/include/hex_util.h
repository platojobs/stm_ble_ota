/**
 * @file    hex_util.h
 * @brief   Tiny hexadecimal helpers (no allocation).
 */

#ifndef HEX_UTIL_H
#define HEX_UTIL_H

#include <stdint.h>
#include <stddef.h>

/**
 * Decode a hex string ("0A1B", "0a:1b", "0a 1b" all accepted).
 * @return 0 on success, -1 on invalid input or output overflow.
 */
int hex_decode(const char *text, uint8_t *out, size_t out_capacity, size_t *out_length);

/** Encode bytes as lowercase hex. Returns characters written or -1. */
int hex_encode(const uint8_t *data, size_t length, char *out, size_t out_capacity);

#endif /* HEX_UTIL_H */
