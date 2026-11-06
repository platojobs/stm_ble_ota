/**
 * @file    mini_json.h
 * @brief   Minimal, allocation-free extractor for flat JSON control messages.
 *
 * Supported: one flat object with string, integer and boolean values.
 * Nested arrays/objects are skipped when looking for a key, so a command such
 * as {"cmd":"wifi.set","ssid":"home","pass":"p@ss"} is handled without a
 * general-purpose JSON library.
 */

#ifndef MINI_JSON_H
#define MINI_JSON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define JSON_OK        0
#define JSON_NOT_FOUND (-1)
#define JSON_INVALID   (-2)

/** Extract a string field; escapes \" \\ \/ \n \r \t are decoded. */
int json_get_string(const char *json, const char *key, char *out, size_t out_size);

/** Extract an unsigned integer field (no sign, decimal only). */
int json_get_u32(const char *json, const char *key, uint32_t *out);

/** Extract a signed integer field. */
int json_get_i32(const char *json, const char *key, int32_t *out);

/** Extract a boolean field. */
int json_get_bool(const char *json, const char *key, bool *out);

/** Escape a string for embedding in a JSON document. Returns 0 on success. */
int json_escape_string(char *out, size_t out_size, const char *input);

#endif /* MINI_JSON_H */
