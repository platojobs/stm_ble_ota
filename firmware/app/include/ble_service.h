// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    ble_service.h
 * @brief   GATT profile state machine on top of the ESP32 AT co-processor.
 *
 * Logical profile (service index 1 in the ESP-AT custom service table):
 *   char 0  device info        read / notify (JSON)
 *   char 1  sensor data        notify (binary sensor_notify_payload_t)
 *   char 2  provisioning       write (JSON), notify result
 *   char 3  OTA control        write (binary frames), notify progress/result
 *   char 4  OTA data           write (binary frames), notify progress/result
 *
 * Incoming writes arrive as +WRITE URCs. Characteristic 2 is JSON provisioning
 * traffic; characteristics 3 and 4 feed the transport-independent frame parser,
 * which reassembles 20-byte BLE writes into complete OTA frames.
 */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include "esp32_at.h"
#include "ota_protocol.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef void (*ble_text_handler_t)(const uint8_t *data, uint32_t length,
                                   void *context);
typedef void (*ble_frame_handler_t)(const uint8_t *data, uint32_t length,
                                    void *context);

int ble_service_init(esp_at_t *at);

void ble_service_set_provision_handler(ble_text_handler_t handler, void *context);
void ble_service_set_ota_handler(ble_frame_handler_t handler, void *context);

void ble_service_on_urc(const esp_at_event_t *event, void *context);

bool ble_service_connected(void);
uint8_t ble_service_connection(void);
/** True when the GATT server was configured and advertising started. */
bool ble_service_ready(void);

/** Send a JSON document on the device-info characteristic. */
int ble_service_notify_info(const char *json);
/** Notify the 20-byte sensor payload on char 1. */
int ble_service_notify_sensor(const sensor_notify_payload_t *payload);
/** Send a JSON provisioning result on char 2. */
int ble_service_notify_provision(const char *json);
/** Send a binary frame (OTA progress/result) on char 3, split to 20-byte writes. */
int ble_service_notify_ota(const uint8_t *frame, uint32_t length);

#endif /* BLE_SERVICE_H */
