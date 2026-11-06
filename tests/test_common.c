#include "test_framework.h"
#include "crc.h"
#include "hex_util.h"
#include "mini_json.h"
#include "ota_protocol.h"
#include "sensor_filter.h"
#include "esp_at_parser.h"

#include <math.h>

TEST_CASE(crc_known_vectors)
{
    const char *check = "123456789";
    ASSERT_EQ(crc32_ieee_buf(check, 9u), 0xCBF43926u);
    ASSERT_EQ(crc16_ccitt_buf(check, 9u), 0x29B1u);

    /* Sensirion example: 0xBE 0xEF -> CRC 0x92 */
    const uint8_t sht[] = {0xBE, 0xEF};
    ASSERT_EQ(crc8_sensirion(sht, 2u), 0x92u);
}

TEST_CASE(hex_roundtrip)
{
    uint8_t data[8];
    size_t length = 0;
    ASSERT_EQ(hex_decode("0a:1B 2c", data, sizeof(data), &length), 0);
    ASSERT_EQ(length, 3u);
    ASSERT_EQ(data[0], 0x0Au);
    ASSERT_EQ(data[1], 0x1Bu);
    ASSERT_EQ(data[2], 0x2Cu);

    char text[16];
    ASSERT_EQ(hex_encode(data, 3u, text, sizeof(text)), 6);
    ASSERT_STREQ(text, "0a1b2c");
    ASSERT_EQ(hex_decode("abc", data, sizeof(data), &length), -1);
}

TEST_CASE(json_extract)
{
    const char *cmd = "{\"cmd\":\"wifi.set\",\"ssid\":\"home \\\"5G\\\"\",\"pass\":\"p@ss\",\"ms\":2500}";
    char cmd_out[32];
    char ssid[40];
    uint32_t ms = 0u;

    ASSERT_EQ(json_get_string(cmd, "cmd", cmd_out, sizeof(cmd_out)), JSON_OK);
    ASSERT_STREQ(cmd_out, "wifi.set");
    ASSERT_EQ(json_get_string(cmd, "ssid", ssid, sizeof(ssid)), JSON_OK);
    ASSERT_STREQ(ssid, "home \"5G\"");
    ASSERT_EQ(json_get_u32(cmd, "ms", &ms), JSON_OK);
    ASSERT_EQ(ms, 2500u);
    ASSERT_EQ(json_get_u32(cmd, "missing", &ms), JSON_NOT_FOUND);

    int32_t signed_value = 0;
    ASSERT_EQ(json_get_i32("{\"v\":-42}", "v", &signed_value), JSON_OK);
    ASSERT_EQ(signed_value, -42);

    bool flag = false;
    ASSERT_EQ(json_get_bool("{\"ok\":true}", "ok", &flag), JSON_OK);
    ASSERT_TRUE(flag);

    ASSERT_EQ(json_get_u32("not json", "v", &ms), JSON_INVALID);
    ASSERT_EQ(json_get_u32("{\"v\":\"text\"}", "v", &ms), JSON_INVALID);
}

TEST_CASE(json_escape)
{
    char out[32];
    ASSERT_EQ(json_escape_string(out, sizeof(out), "a\"b\\c"), 0);
    ASSERT_STREQ(out, "a\\\"b\\\\c");
}

TEST_CASE(frame_roundtrip_fragmented)
{
    uint8_t payload[100];
    for (uint8_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = i;
    }
    uint8_t frame[STM_FRAME_MAX_SIZE];
    uint32_t frame_len = stm_frame_build(STM_FRAME_OTA_DATA, 0u, 7u, 12u,
                                         payload, sizeof(payload), frame, sizeof(frame));
    ASSERT_EQ(frame_len, STM_FRAME_HEADER_SIZE + sizeof(payload));

    stm_frame_parser_t parser;
    stm_frame_parser_init(&parser);

    /* Simulate 20-byte BLE writes. */
    uint32_t delivered = 0u;
    uint32_t offset = 0u;
    while (offset < frame_len) {
        uint32_t n = frame_len - offset;
        if (n > 20u) {
            n = 20u;
        }
        delivered += stm_frame_parser_feed(&parser, frame + offset, n, NULL, NULL);
        offset += n;
    }
    ASSERT_EQ(delivered, 1u);
    ASSERT_EQ(parser.length, 0u);
}

typedef struct {
    stm_frame_t frame;
    uint8_t data[STM_FRAME_MAX_PAYLOAD];
} captured_frame_t;

static void capture_cb(const stm_frame_t *frame, void *context)
{
    captured_frame_t *captured = (captured_frame_t *)context;
    captured->frame = *frame;
    memcpy(captured->data, frame->payload, frame->payload_len);
    captured->frame.payload = captured->data;
}

TEST_CASE(frame_bad_crc_resync)
{
    uint8_t frame[STM_FRAME_MAX_SIZE];
    uint8_t payload[4] = {1, 2, 3, 4};
    uint32_t len = stm_frame_build(STM_FRAME_CMD_JSON, 0u, 1u, 0u, payload, 4u,
                                   frame, sizeof(frame));
    frame[STM_FRAME_HEADER_SIZE + 1u] ^= 0xFFu; /* corrupt payload */

    stm_frame_parser_t parser;
    stm_frame_parser_init(&parser);
    captured_frame_t captured;
    memset(&captured, 0, sizeof(captured));

    ASSERT_EQ(stm_frame_parser_feed(&parser, frame, len, capture_cb, &captured), 0u);
    ASSERT_EQ(parser.frames_bad, 1u);

    uint8_t good[STM_FRAME_MAX_SIZE];
    uint32_t good_len = stm_frame_build(STM_FRAME_CMD_JSON, 0u, 2u, 0u, payload, 4u,
                                        good, sizeof(good));
    ASSERT_EQ(stm_frame_parser_feed(&parser, good, good_len, capture_cb, &captured), 1u);
    ASSERT_EQ(captured.frame.seq, 2u);
    ASSERT_EQ(captured.frame.payload_len, 4u);
}

TEST_CASE(frame_invalid_length_rejected)
{
    uint8_t frame[STM_FRAME_MAX_SIZE];
    memset(frame, 0, sizeof(frame));
    frame[0] = STM_FRAME_SYNC0;
    frame[1] = STM_FRAME_SYNC1;
    frame[2] = STM_FRAME_CMD_JSON;
    frame[6] = 0xFFu; /* payload_len = 0x0FFF, above the limit */
    frame[7] = 0x0Fu;

    stm_frame_parser_t parser;
    stm_frame_parser_init(&parser);
    ASSERT_EQ(stm_frame_parser_feed(&parser, frame, 20u, NULL, NULL), 0u);
    ASSERT_EQ(parser.frames_bad, 1u);
}

TEST_CASE(sensor_filter_ema)
{
    ema_filter_t filter;
    ema_filter_init(&filter, 0.25f, 0.0f);
    ASSERT_TRUE(fabsf(ema_filter_update(&filter, 20.0f) - 20.0f) < 0.001f);
    ASSERT_TRUE(fabsf(ema_filter_update(&filter, 24.0f) - 21.0f) < 0.001f);
    ASSERT_TRUE(fabsf(ema_filter_update(&filter, 21.0f) - 21.0f) < 0.001f);

    ema_filter_init(&filter, 1.0f, 2.0f);
    (void)ema_filter_update(&filter, 0.0f);
    /* A 100-degree spike is clamped to +2. */
    ASSERT_TRUE(fabsf(ema_filter_update(&filter, 100.0f) - 2.0f) < 0.001f);
}

TEST_CASE(sensor_filter_moving_average)
{
    moving_average_t avg;
    moving_average_init(&avg);
    for (uint8_t i = 0; i < 8u; ++i) {
        moving_average_add(&avg, (float)(i + 1u));
    }
    ASSERT_TRUE(fabsf(moving_average_value(&avg) - 4.5f) < 0.001f);
    moving_average_add(&avg, 100.0f);
    /* Window now holds 2..8 and 100 -> (35 + 100) / 8 = 16.875. */
    ASSERT_TRUE(fabsf(moving_average_value(&avg) - 16.875f) < 0.001f);
}

TEST_CASE(esp_at_write_line)
{
    esp_at_event_t event;
    /* Four-field form: conn, srv, char, len, hex value. */
    ASSERT_EQ(esp_at_parse_line("+WRITE:0,1,3,10,0a1b2c3d4e5f60718293", &event),
              ESP_AT_EVT_BLE_WRITE);
    ASSERT_EQ(event.conn_index, 0u);
    ASSERT_EQ(event.srv_index, 1u);
    ASSERT_EQ(event.char_index, 3u);
    ASSERT_EQ(event.data_length, 10u);
    ASSERT_EQ(event.data[0], 0x0Au);
    ASSERT_EQ(event.data[1], 0x1Bu);

    /* Five-field form with descriptor index. */
    ASSERT_EQ(esp_at_parse_line("+WRITE:0,1,5,2,4,0a0b0c0d", &event),
              ESP_AT_EVT_BLE_WRITE);
    ASSERT_EQ(event.desc_index, 2u);
    ASSERT_EQ(event.data_length, 4u);
    ASSERT_EQ(event.data[3], 0x0Du);
}

TEST_CASE(esp_at_other_lines)
{
    esp_at_event_t event;
    ASSERT_EQ(esp_at_parse_line("OK", &event), ESP_AT_EVT_OK);
    ASSERT_EQ(esp_at_parse_line("ERROR", &event), ESP_AT_EVT_ERROR);
    ASSERT_EQ(esp_at_parse_line("ready", &event), ESP_AT_EVT_READY);
    ASSERT_EQ(esp_at_parse_line(">", &event), ESP_AT_EVT_PROMPT);
    ASSERT_EQ(esp_at_parse_line("WIFI GOT IP", &event), ESP_AT_EVT_WIFI_GOT_IP);
    ASSERT_EQ(esp_at_parse_line("+BLECONN:0,1a:2b:3c:4d:5e:6f", &event),
              ESP_AT_EVT_BLE_CONNECTED);
    ASSERT_EQ(esp_at_parse_line("+BLEDISCONN:0", &event), ESP_AT_EVT_BLE_DISCONNECTED);
    ASSERT_EQ(esp_at_parse_line("+READ:0,1,4", &event), ESP_AT_EVT_BLE_READ);
    ASSERT_EQ(event.char_index, 4u);
    ASSERT_EQ(esp_at_parse_line("+CWJAP:\"ssid\",\"bssid\",6", &event),
              ESP_AT_EVT_CWJAP_RESULT);
}

void run_common_tests(void)
{
    RUN_TEST(crc_known_vectors);
    RUN_TEST(hex_roundtrip);
    RUN_TEST(json_extract);
    RUN_TEST(json_escape);
    RUN_TEST(frame_roundtrip_fragmented);
    RUN_TEST(frame_bad_crc_resync);
    RUN_TEST(frame_invalid_length_rejected);
    RUN_TEST(sensor_filter_ema);
    RUN_TEST(sensor_filter_moving_average);
    RUN_TEST(esp_at_write_line);
    RUN_TEST(esp_at_other_lines);
}
