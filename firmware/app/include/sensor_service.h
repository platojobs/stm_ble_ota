// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    sensor_service.h
 * @brief   Periodic sampling, filtering and reporting of sensors.
 */

#ifndef SENSOR_SERVICE_H
#define SENSOR_SERVICE_H

#include "ota_protocol.h"

#include <stdint.h>
#include <stddef.h>

typedef void (*sensor_report_cb_t)(const sensor_notify_payload_t *payload,
                                   void *context);

/** Initialise I2C, ADC and the filters. Missing sensors degrade gracefully. */
void sensor_service_init(void);

/** Call from the main loop at any rate; samples every SENSOR_PERIOD_MS. */
void sensor_service_tick(uint32_t now_ms);

void sensor_service_set_report_callback(sensor_report_cb_t callback,
                                        void *context);

/** Latest value, even if some channels are invalid (check valid_mask). */
const sensor_notify_payload_t *sensor_service_last(void);

#endif /* SENSOR_SERVICE_H */
