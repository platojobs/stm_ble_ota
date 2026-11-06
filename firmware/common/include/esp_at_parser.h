/**
 * @file    esp_at_parser.h
 * @brief   Classification of ESP-AT response and URC lines.
 *
 * References: Espressif ESP-AT User Guide, "Bluetooth LE AT Commands" and
 * "BLE AT Examples". A client write to a GATT server characteristic produces
 *
 *   +WRITE:<conn>,<srv>,<char>,[<desc>],<len>,<value>
 *
 * where <value> is a hexadecimal string. This parser accepts both the
 * 4-field and 5-field forms and decodes the value.
 */

#ifndef ESP_AT_PARSER_H
#define ESP_AT_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#define ESP_AT_MAX_WRITE 256u

typedef enum {
    ESP_AT_EVT_UNKNOWN = 0,
    ESP_AT_EVT_OK,
    ESP_AT_EVT_ERROR,
    ESP_AT_EVT_READY,
    ESP_AT_EVT_PROMPT,          /* '>' prompt: module expects raw payload      */
    ESP_AT_EVT_BLE_CONNECTED,
    ESP_AT_EVT_BLE_DISCONNECTED,
    ESP_AT_EVT_BLE_WRITE,       /* client wrote a characteristic               */
    ESP_AT_EVT_BLE_READ,        /* client read a characteristic                */
    ESP_AT_EVT_WIFI_CONNECTED,
    ESP_AT_EVT_WIFI_GOT_IP,
    ESP_AT_EVT_WIFI_DISCONNECTED,
    ESP_AT_EVT_CWJAP_RESULT,    /* +CWJAP:<ssid>,<bssid>,<channel>              */
    ESP_AT_EVT_CIPSTATUS,       /* +CIPSTATUS:<link id>,...                    */
    ESP_AT_EVT_BUSY,            /* "busy p..."                                  */
} esp_at_event_type_t;

typedef struct {
    esp_at_event_type_t type;
    uint8_t conn_index;
    uint8_t srv_index;
    uint8_t char_index;
    uint8_t desc_index;         /* 0xFF when absent                            */
    uint16_t data_length;
    uint8_t  data[ESP_AT_MAX_WRITE];
    bool     data_truncated;
} esp_at_event_t;

/** Parse one line (without CR/LF). Returns the event type. */
esp_at_event_type_t esp_at_parse_line(const char *line, esp_at_event_t *event);

#endif /* ESP_AT_PARSER_H */
