// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    ble_service.c
 * @brief   ESP-AT GATT setup, URC routing and notification helpers.
 *
 * ESP-AT addresses a characteristic as (conn, srv, char). The custom service
 * table compiled into the ESP32 must declare our UUIDs at service index 1,
 * characteristics 0..4 (see docs/esp32_at_custom_service.md).
 */

#include "ble_service.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>

static esp_at_t *g_at;
static ble_text_handler_t g_provision_handler;
static void *g_provision_context;
static ble_frame_handler_t g_ota_handler;
static void *g_ota_context;
static bool g_connected;
static bool g_ready;
static uint8_t g_conn;

static int command(const char *cmd, uint32_t timeout_ms)
{
    return esp_at_exec(g_at, cmd, timeout_ms, false);
}

int ble_service_init(esp_at_t *at)
{
    g_at = at;
    g_connected = false;
    g_ready = false;
    g_conn = 0u;

    /* The AT link itself is verified by wifi_service_init() before this runs. */
    (void)command("AT+BLEINIT=2", ESP_COMMAND_TIMEOUT_MS); /* GATT server */
    (void)command("AT+BLENAME=\"" BLE_DEVICE_NAME "\"", ESP_COMMAND_TIMEOUT_MS);
    (void)command("AT+BLEGATTSSRVCRE", ESP_COMMAND_TIMEOUT_MS);
    (void)command("AT+BLEGATTSSRVSTART", ESP_COMMAND_TIMEOUT_MS);

    /* Advertise flags + the complete 16-bit service UUID 0xFF00. */
    (void)command("AT+BLEADVPARAM=50,150,0,0,7,0,0,0", ESP_COMMAND_TIMEOUT_MS);
    (void)command("AT+BLEADVDATA=\"" BLE_ADV_DATA "\"",
                  ESP_COMMAND_TIMEOUT_MS);
    g_ready = command("AT+BLEADVSTART", ESP_COMMAND_TIMEOUT_MS) == ESP_AT_EXEC_OK;
    return g_ready ? 0 : -1;
}

void ble_service_set_provision_handler(ble_text_handler_t handler, void *context)
{
    g_provision_handler = handler;
    g_provision_context = context;
}

void ble_service_set_ota_handler(ble_frame_handler_t handler, void *context)
{
    g_ota_handler = handler;
    g_ota_context = context;
}

void ble_service_on_urc(const esp_at_event_t *event, void *context)
{
    (void)context;
    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case ESP_AT_EVT_BLE_CONNECTED:
        g_connected = true;
        g_conn = event->conn_index;
        break;
    case ESP_AT_EVT_BLE_DISCONNECTED:
        g_connected = false;
        break;
    case ESP_AT_EVT_BLE_WRITE:
        if (event->srv_index != BLE_SERVICE_INDEX) {
            break;
        }
        if (event->char_index == BLE_CHAR_PROVISION) {
            if (g_provision_handler != NULL) {
                g_provision_handler(event->data, event->data_length,
                                    g_provision_context);
            }
        } else if (event->char_index == BLE_CHAR_OTA_CONTROL ||
                   event->char_index == BLE_CHAR_OTA_DATA) {
            if (g_ota_handler != NULL) {
                g_ota_handler(event->data, event->data_length, g_ota_context);
            }
        }
        break;
    default:
        break;
    }
}

bool ble_service_connected(void)
{
    return g_connected;
}

uint8_t ble_service_connection(void)
{
    return g_conn;
}

bool ble_service_ready(void)
{
    return g_ready;
}

static int notify(uint8_t characteristic, const void *payload, uint32_t length)
{
    if (!g_connected || length == 0u) {
        return -1;
    }
    /* 20-byte writes are safe with the default ATT MTU (23). */
    const uint8_t *bytes = (const uint8_t *)payload;
    uint32_t offset = 0u;
    while (offset < length) {
        uint32_t chunk = length - offset;
        if (chunk > 20u) {
            chunk = 20u;
        }
        if (esp_at_ble_notify(g_at, g_conn, BLE_SERVICE_INDEX, characteristic,
                              bytes + offset, (uint16_t)chunk,
                              ESP_COMMAND_TIMEOUT_MS) != 0) {
            return -1;
        }
        offset += chunk;
    }
    return 0;
}

int ble_service_notify_info(const char *json)
{
    return notify(BLE_CHAR_INFO, json, (uint32_t)strlen(json));
}

int ble_service_notify_sensor(const sensor_notify_payload_t *payload)
{
    if (payload == NULL) {
        return -1;
    }
    return notify(BLE_CHAR_SENSOR, payload, sizeof(*payload));
}

int ble_service_notify_provision(const char *json)
{
    return notify(BLE_CHAR_PROVISION, json, (uint32_t)strlen(json));
}

int ble_service_notify_ota(const uint8_t *frame, uint32_t length)
{
    return notify(BLE_CHAR_OTA_CONTROL, frame, length);
}
