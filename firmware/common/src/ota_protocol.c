#include "ota_protocol.h"
#include "crc.h"

#include <string.h>

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(stm_frame_header_t) == STM_FRAME_HEADER_SIZE, "frame header size");
_Static_assert(sizeof(ota_begin_payload_t) == 20u, "ota_begin payload size");
_Static_assert(sizeof(ota_end_payload_t) == 8u, "ota_end payload size");
_Static_assert(sizeof(ota_progress_payload_t) == 16u, "ota_progress payload size");
_Static_assert(sizeof(sensor_notify_payload_t) == 20u, "sensor payload size");
#endif

/* Read the header without relying on unaligned struct loads. */
static void header_decode(const uint8_t *raw, stm_frame_header_t *hdr)
{
    hdr->sync0         = raw[0];
    hdr->sync1         = raw[1];
    hdr->type          = raw[2];
    hdr->flags         = raw[3];
    hdr->seq           = (uint16_t)((uint16_t)raw[4] | ((uint16_t)raw[5] << 8u));
    hdr->payload_len   = (uint16_t)((uint16_t)raw[6] | ((uint16_t)raw[7] << 8u));
    hdr->offset        = ((uint32_t)raw[8]  | ((uint32_t)raw[9]  << 8u) |
                          ((uint32_t)raw[10] << 16u) | ((uint32_t)raw[11] << 24u));
    hdr->payload_crc32 = ((uint32_t)raw[12] | ((uint32_t)raw[13] << 8u) |
                          ((uint32_t)raw[14] << 16u) | ((uint32_t)raw[15] << 24u));
}

static void header_encode(const stm_frame_header_t *hdr, uint8_t *raw)
{
    raw[0]  = hdr->sync0;
    raw[1]  = hdr->sync1;
    raw[2]  = hdr->type;
    raw[3]  = hdr->flags;
    raw[4]  = (uint8_t)(hdr->seq & 0xFFu);
    raw[5]  = (uint8_t)((hdr->seq >> 8u) & 0xFFu);
    raw[6]  = (uint8_t)(hdr->payload_len & 0xFFu);
    raw[7]  = (uint8_t)((hdr->payload_len >> 8u) & 0xFFu);
    raw[8]  = (uint8_t)(hdr->offset & 0xFFu);
    raw[9]  = (uint8_t)((hdr->offset >> 8u) & 0xFFu);
    raw[10] = (uint8_t)((hdr->offset >> 16u) & 0xFFu);
    raw[11] = (uint8_t)((hdr->offset >> 24u) & 0xFFu);
    raw[12] = (uint8_t)(hdr->payload_crc32 & 0xFFu);
    raw[13] = (uint8_t)((hdr->payload_crc32 >> 8u) & 0xFFu);
    raw[14] = (uint8_t)((hdr->payload_crc32 >> 16u) & 0xFFu);
    raw[15] = (uint8_t)((hdr->payload_crc32 >> 24u) & 0xFFu);
}

void stm_frame_parser_init(stm_frame_parser_t *parser)
{
    memset(parser, 0, sizeof(*parser));
}

static void discard_prefix(stm_frame_parser_t *parser, uint16_t count)
{
    if (count >= parser->length) {
        parser->bytes_dropped += parser->length;
        parser->length = 0;
        return;
    }
    memmove(parser->buffer, parser->buffer + count,
            (size_t)(parser->length - count));
    parser->length = (uint16_t)(parser->length - count);
    parser->bytes_dropped += count;
}

/* Find the first sync pair. Returns index or -1. */
static int find_sync(const uint8_t *buf, uint16_t len)
{
    if (len < 2u) {
        return -1;
    }
    for (uint16_t i = 0; i + 1u < len; ++i) {
        if (buf[i] == STM_FRAME_SYNC0 && buf[i + 1u] == STM_FRAME_SYNC1) {
            return (int)i;
        }
    }
    return -1;
}

uint32_t stm_frame_parser_feed(stm_frame_parser_t *parser,
                               const uint8_t *data,
                               uint32_t length,
                               stm_frame_callback_t cb,
                               void *context)
{
    uint32_t delivered = 0;

    if (length == 0u) {
        return 0u;
    }

    /* Append, but never overflow. A full buffer without a valid frame means
     * the stream is corrupt; drop the oldest bytes and resynchronise. */
    if (length >= STM_FRAME_PARSER_BUFFER) {
        /* Keep only the tail; a frame cannot be larger than the buffer. */
        data += (length - STM_FRAME_PARSER_BUFFER);
        length = STM_FRAME_PARSER_BUFFER;
        parser->bytes_dropped += parser->length;
        parser->length = 0;
    }
    if ((uint32_t)parser->length + length > STM_FRAME_PARSER_BUFFER) {
        uint16_t need = (uint16_t)((uint32_t)parser->length + length -
                                   STM_FRAME_PARSER_BUFFER);
        discard_prefix(parser, need);
    }
    memcpy(parser->buffer + parser->length, data, (size_t)length);
    parser->length = (uint16_t)(parser->length + (uint16_t)length);

    for (;;) {
        int sync_pos = find_sync(parser->buffer, parser->length);
        if (sync_pos < 0) {
            /* Keep the last byte: it may be the first half of a sync pair. */
            if (parser->length > 1u) {
                discard_prefix(parser, (uint16_t)(parser->length - 1u));
            }
            break;
        }
        if (sync_pos > 0) {
            discard_prefix(parser, (uint16_t)sync_pos);
        }
        if (parser->length < STM_FRAME_HEADER_SIZE) {
            break; /* need more bytes */
        }

        stm_frame_header_t hdr;
        header_decode(parser->buffer, &hdr);

        if (hdr.payload_len > STM_FRAME_MAX_PAYLOAD) {
            parser->frames_bad++;
            discard_prefix(parser, 1u); /* resync after this candidate */
            continue;
        }

        uint16_t total = (uint16_t)(STM_FRAME_HEADER_SIZE + hdr.payload_len);
        if (parser->length < total) {
            break; /* need more bytes */
        }

        const uint8_t *payload = parser->buffer + STM_FRAME_HEADER_SIZE;
        uint32_t crc = crc32_ieee_buf(payload, hdr.payload_len);
        if (crc != hdr.payload_crc32) {
            parser->frames_bad++;
            discard_prefix(parser, 1u);
            continue;
        }

        stm_frame_t frame;
        frame.type         = hdr.type;
        frame.flags        = hdr.flags;
        frame.seq          = hdr.seq;
        frame.payload_len  = hdr.payload_len;
        frame.offset       = hdr.offset;
        frame.payload_crc32 = hdr.payload_crc32;
        frame.payload      = payload;

        parser->frames_ok++;
        delivered++;
        if (cb != NULL) {
            cb(&frame, context);
        }

        discard_prefix(parser, total);
    }

    return delivered;
}

uint32_t stm_frame_build(uint8_t type,
                         uint8_t flags,
                         uint16_t seq,
                         uint32_t offset,
                         const void *payload,
                         uint16_t payload_len,
                         uint8_t *out,
                         uint32_t out_capacity)
{
    if (out == NULL || payload_len > STM_FRAME_MAX_PAYLOAD) {
        return 0u;
    }
    uint32_t total = (uint32_t)STM_FRAME_HEADER_SIZE + payload_len;
    if (out_capacity < total) {
        return 0u;
    }

    stm_frame_header_t hdr;
    hdr.sync0 = STM_FRAME_SYNC0;
    hdr.sync1 = STM_FRAME_SYNC1;
    hdr.type = type;
    hdr.flags = flags;
    hdr.seq = seq;
    hdr.payload_len = payload_len;
    hdr.offset = offset;
    hdr.payload_crc32 = (payload_len == 0u || payload == NULL)
                            ? 0u
                            : crc32_ieee_buf(payload, payload_len);

    header_encode(&hdr, out);
    if (payload_len > 0u && payload != NULL) {
        memcpy(out + STM_FRAME_HEADER_SIZE, payload, payload_len);
    }
    return total;
}

uint32_t ota_version_pack(uint8_t major, uint8_t minor, uint8_t patch)
{
    return ((uint32_t)major << 16u) | ((uint32_t)minor << 8u) | (uint32_t)patch;
}
