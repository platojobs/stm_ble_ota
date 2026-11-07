// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "record_store.h"
#include "crc.h"

#include <string.h>

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t sequence;
    uint32_t payload_len;
    uint32_t payload_crc32;
} record_header_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(record_header_t) == RECORD_HEADER_LEN, "record header size");
#endif

static int read_record_header(const record_store_t *store, uint32_t page,
                              record_header_t *hdr)
{
    if (store->flash->read(store->flash->context, page, hdr, sizeof(*hdr)) != 0) {
        return -1;
    }
    if (hdr->magic != RECORD_MAGIC) {
        return -1;
    }
    if (hdr->payload_len > store->max_payload) {
        return -1;
    }
    return 0;
}

int record_store_init(record_store_t *store, const record_flash_if_t *flash,
                      uint32_t page_a, uint32_t page_b, uint16_t max_payload)
{
    if (store == NULL || flash == NULL || flash->erase_page == NULL ||
        flash->program == NULL || flash->read == NULL) {
        return -1;
    }
    if (max_payload > RECORD_MAX_PAYLOAD) {
        return -1;
    }
    memset(store, 0, sizeof(*store));
    store->flash = flash;
    store->page_a = page_a;
    store->page_b = page_b;
    store->max_payload = max_payload;
    return 0;
}

static int pick_valid(const record_store_t *store, record_info_t *info, bool *found_a,
                      bool *found_b)
{
    uint32_t page[2] = {store->page_a, store->page_b};
    record_header_t hdr;
    bool valid[2] = {false, false};

    for (uint8_t i = 0; i < 2u; ++i) {
        if (read_record_header(store, page[i], &hdr) == 0) {
            /* Verify payload CRC without a large stack buffer. */
            uint32_t crc = 0xFFFFFFFFu;
            uint8_t chunk[32];
            uint32_t remaining = hdr.payload_len;
            uint32_t addr = page[i] + RECORD_HEADER_LEN;
            while (remaining > 0u) {
                uint32_t n = remaining > sizeof(chunk) ? (uint32_t)sizeof(chunk) : remaining;
                if (store->flash->read(store->flash->context, addr, chunk, n) != 0) {
                    break;
                }
                crc = crc32_ieee(chunk, n, crc);
                addr += n;
                remaining -= n;
            }
            if (remaining == 0u) {
                crc ^= 0xFFFFFFFFu;
                if (crc == hdr.payload_crc32) {
                    valid[i] = true;
                }
            }
        }
    }

    if (found_a != NULL) {
        *found_a = valid[0];
    }
    if (found_b != NULL) {
        *found_b = valid[1];
    }

    if (!valid[0] && !valid[1]) {
        return -1;
    }

    uint8_t chosen;
    if (valid[0] && valid[1]) {
        uint32_t seq_a = 0, seq_b = 0;
        if (read_record_header(store, page[0], &hdr) == 0) {
            seq_a = hdr.sequence;
        }
        if (read_record_header(store, page[1], &hdr) == 0) {
            seq_b = hdr.sequence;
        }
        chosen = (seq_a >= seq_b) ? 0u : 1u;
    } else {
        chosen = valid[0] ? 0u : 1u;
    }

    read_record_header(store, page[chosen], &hdr);
    info->sequence = hdr.sequence;
    info->payload_len = hdr.payload_len;
    info->page = page[chosen];
    return 0;
}

int record_store_load(const record_store_t *store, void *out, uint32_t out_capacity,
                      record_info_t *info)
{
    record_info_t local;
    if (pick_valid(store, &local, NULL, NULL) != 0) {
        return -1;
    }
    if (local.payload_len > out_capacity) {
        return -1;
    }
    if (store->flash->read(store->flash->context, local.page + RECORD_HEADER_LEN,
                           out, local.payload_len) != 0) {
        return -1;
    }
    if (info != NULL) {
        *info = local;
    }
    return 0;
}

int record_store_save(record_store_t *store, const void *payload, uint32_t payload_len,
                      uint32_t *new_sequence)
{
    if (payload_len > store->max_payload) {
        return -1;
    }

    record_info_t info;
    bool valid_a = false;
    bool valid_b = false;
    uint32_t sequence = 0u;
    uint32_t target_page = store->page_a;

    if (pick_valid(store, &info, &valid_a, &valid_b) == 0) {
        sequence = info.sequence + 1u;
        /* Write to the page that does not currently hold the newest record. */
        target_page = (info.page == store->page_a) ? store->page_b : store->page_a;
    } else {
        sequence = 1u;
        target_page = store->page_a;
    }

    if (store->flash->erase_page(store->flash->context, target_page) != 0) {
        return -1;
    }
    if (payload_len > 0u && payload == NULL) {
        return -1;
    }

    record_header_t hdr;
    hdr.magic = RECORD_MAGIC;
    hdr.sequence = sequence;
    hdr.payload_len = payload_len;
    hdr.payload_crc32 = (payload_len == 0u) ? 0u : crc32_ieee_buf(payload, payload_len);
    if (store->flash->program(store->flash->context, target_page, &hdr,
                              (uint32_t)sizeof(hdr)) != 0) {
        return -1;
    }

    /* Stream the payload in 16-byte granules, padding the tail with 0xFF. */
    uint8_t granule[16];
    uint32_t offset = 0u;
    while (offset < payload_len) {
        uint32_t remaining = payload_len - offset;
        uint32_t n = remaining > sizeof(granule) ? (uint32_t)sizeof(granule) : remaining;
        if (n < sizeof(granule)) {
            memset(granule, 0xFF, sizeof(granule));
        }
        memcpy(granule, (const uint8_t *)payload + offset, n);
        if (store->flash->program(store->flash->context,
                                  target_page + RECORD_HEADER_LEN + offset,
                                  granule, sizeof(granule)) != 0) {
            return -1;
        }
        offset += n;
    }

    if (new_sequence != NULL) {
        *new_sequence = sequence;
    }
    return 0;
}

int record_store_clear(record_store_t *store)
{
    if (store->flash->erase_page(store->flash->context, store->page_a) != 0) {
        return -1;
    }
    return store->flash->erase_page(store->flash->context, store->page_b);
}
