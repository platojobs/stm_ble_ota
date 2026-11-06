/**
 * @file    ota_protocol.h
 * @brief   Transport-independent frame format shared by BLE, the direct UART
 *          test link and the bootloader metadata path.
 *
 * Every frame starts with a 16-byte header:
 *
 *   offset  size  field
 *   0       2     sync = 0xA5 0x5A
 *   2       1     type
 *   3       1     flags (reserved, must be 0)
 *   4       2     seq (monotonically increasing per sender, wraps at 65535)
 *   6       2     payload_len (0..512)
 *   8       4     offset (byte offset inside the target image, DATA only)
 *   12      4     payload_crc32 (CRC-32/IEEE over payload bytes)
 *   16      N     payload
 *
 * Frames are byte streams. On BLE a single write may carry a partial frame
 * (default MTU gives 20 usable bytes), and the parser reassembles across
 * writes. A frame with a bad CRC or an impossible length is discarded and the
 * parser resynchronises on the next 0xA5 0x5A pair.
 */

#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define STM_FRAME_SYNC0          0xA5u
#define STM_FRAME_SYNC1          0x5Au
#define STM_FRAME_HEADER_SIZE    16u
#define STM_FRAME_MAX_PAYLOAD    512u
#define STM_FRAME_MAX_SIZE       (STM_FRAME_HEADER_SIZE + STM_FRAME_MAX_PAYLOAD)
#define STM_FRAME_PARSER_BUFFER  (STM_FRAME_MAX_SIZE)

/* Frame types. Control traffic uses JSON payloads; OTA bulk data is binary. */
typedef enum {
    STM_FRAME_CMD_JSON      = 0x01, /* host -> device, JSON command           */
    STM_FRAME_ACK_JSON      = 0x02, /* device -> host, JSON result            */
    STM_FRAME_NACK_JSON     = 0x03, /* device -> host, JSON error             */
    STM_FRAME_SENSOR_NOTIFY = 0x04, /* device -> host, sensor_notify_payload  */
    STM_FRAME_STATUS_NOTIFY = 0x05, /* device -> host, JSON status            */
    STM_FRAME_OTA_BEGIN     = 0x10, /* host -> device, ota_begin_payload      */
    STM_FRAME_OTA_DATA      = 0x11, /* host -> device, raw image bytes        */
    STM_FRAME_OTA_END       = 0x12, /* host -> device, ota_end_payload        */
    STM_FRAME_OTA_ABORT     = 0x13, /* host -> device, no payload             */
    STM_FRAME_OTA_PROGRESS  = 0x14, /* device -> host, ota_progress_payload   */
    STM_FRAME_OTA_RESULT    = 0x15, /* device -> host, ota_progress_payload   */
} stm_frame_type_t;

/** OTA transfer states, also encoded in ota_progress_payload_t.state. */
typedef enum {
    OTA_STATE_IDLE          = 0, /* no transfer in progress                     */
    OTA_STATE_RECEIVING     = 1, /* accepting DATA frames                       */
    OTA_STATE_VERIFYING     = 2, /* END received, CRC/size verification running */
    OTA_STATE_READY_TO_BOOT = 3, /* metadata written, reboot pending            */
    OTA_STATE_INSTALLING    = 4, /* bootloader is copying slot B -> slot A       */
    OTA_STATE_INSTALLED     = 5, /* new image installed and verified            */
    OTA_STATE_ERROR         = 6, /* transfer aborted or verification failed     */
} ota_state_t;

/** Error reason codes used by NACK frames and OTA results. */
typedef enum {
    OTA_REASON_NONE          = 0,
    OTA_REASON_BAD_STATE     = 1,
    OTA_REASON_BAD_SIZE      = 2,
    OTA_REASON_BAD_CRC       = 3,
    OTA_REASON_OUT_OF_ORDER  = 4,
    OTA_REASON_FLASH_ERROR   = 5,
    OTA_REASON_TIMEOUT       = 6,
    OTA_REASON_ABORTED       = 7,
    OTA_REASON_BAD_VERSION   = 8,
} ota_reason_t;

typedef struct __attribute__((packed)) {
    uint8_t  sync0;
    uint8_t  sync1;
    uint8_t  type;
    uint8_t  flags;
    uint16_t seq;
    uint16_t payload_len;
    uint32_t offset;
    uint32_t payload_crc32;
} stm_frame_header_t;

typedef struct __attribute__((packed)) {
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t version;       /* 0x00MMmmpp: major, minor, patch                 */
    uint32_t build_id;
    uint16_t hardware_id;
    uint16_t reserved;
} ota_begin_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t image_size;
    uint32_t image_crc32;
} ota_end_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t  state;         /* ota_state_t                                     */
    uint8_t  reason;        /* ota_reason_t                                    */
    uint16_t reserved;
    uint32_t received;      /* bytes accepted by the device                    */
    uint32_t total;         /* announced image size                            */
    uint32_t crc32;         /* running CRC-32 of accepted bytes                */
} ota_progress_payload_t;

/** Sensor notification payload (20 bytes, sent on the sensor characteristic). */
typedef struct __attribute__((packed)) {
    uint8_t  payload_type;      /* 0x01                                          */
    uint8_t  valid_mask;        /* bit0 temperature, bit1 humidity, bit2 light    */
    uint16_t reserved;
    uint32_t uptime_ms;
    int32_t  temperature_centi; /* degrees Celsius * 100                          */
    int32_t  humidity_centi;    /* %RH * 100                                      */
    uint16_t light_mv;
    uint16_t reserved2;
} sensor_notify_payload_t;

/** A complete frame handed to the consumer. Payload points into the parser. */
typedef struct {
    uint8_t        type;
    uint8_t        flags;
    uint16_t       seq;
    uint16_t       payload_len;
    uint32_t       offset;
    uint32_t       payload_crc32;
    const uint8_t *payload;
} stm_frame_t;

typedef struct {
    uint8_t  buffer[STM_FRAME_PARSER_BUFFER];
    uint16_t length;
    uint32_t frames_ok;
    uint32_t frames_bad;
    uint32_t bytes_dropped;
} stm_frame_parser_t;

typedef void (*stm_frame_callback_t)(const stm_frame_t *frame, void *context);

void stm_frame_parser_init(stm_frame_parser_t *parser);

/**
 * Feed raw bytes into the parser.
 * @return number of complete, CRC-valid frames delivered through @p cb.
 */
uint32_t stm_frame_parser_feed(stm_frame_parser_t *parser,
                               const uint8_t *data,
                               uint32_t length,
                               stm_frame_callback_t cb,
                               void *context);

/**
 * Build a complete frame into @p out.
 * @return total frame size, or 0 when the arguments are invalid / do not fit.
 */
uint32_t stm_frame_build(uint8_t type,
                         uint8_t flags,
                         uint16_t seq,
                         uint32_t offset,
                         const void *payload,
                         uint16_t payload_len,
                         uint8_t *out,
                         uint32_t out_capacity);

/** Encode 0x00MMmmpp from three decimal components (each 0..255). */
uint32_t ota_version_pack(uint8_t major, uint8_t minor, uint8_t patch);

#endif /* OTA_PROTOCOL_H */
