/**
 * @file    wifi_service.h
 * @brief   WiFi provisioning, persistence and reconnect on top of ESP-AT.
 *
 * Commands are JSON documents (from BLE char 2, the PC test link, or both):
 *   {"cmd":"wifi.set","ssid":"...","password":"..."}
 *   {"cmd":"wifi.clear"}
 *   {"cmd":"wifi.status"}
 *
 * On success credentials are written to the config pages and the device
 * reconnects automatically on every boot when auto-connect is enabled.
 */

#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include "esp32_at.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef void (*wifi_reply_cb_t)(const char *json, void *context);

int wifi_service_init(esp_at_t *at);

void wifi_service_set_reply_callback(wifi_reply_cb_t callback, void *context);

/** Handle a provisioning command. Returns 0 when the command was accepted. */
int wifi_service_handle_command(const char *json, uint32_t length);

/** Retry policy for dropped connections. Call from the main loop. */
void wifi_service_tick(uint32_t now_ms);

/** Route ESP-AT URCs (WiFi connected/disconnected) into this service. */
void wifi_service_on_urc(const esp_at_event_t *event, void *context);

bool wifi_service_connected(void);
const char *wifi_service_ssid(void);

/** Fill @p out with a status JSON object. */
void wifi_service_status_json(char *out, size_t out_size);

#endif /* WIFI_SERVICE_H */
