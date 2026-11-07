// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    device_service.h
 * @brief   Manufacturing identity of the unit: serial and hardware id.
 *
 * On first boot the serial is derived from the MCU 96-bit UID and persisted,
 * so every board reports a stable, unique identity without a factory step.
 * A production line may pre-program the same record; factory reset deliberately
 * keeps it.
 */

#ifndef DEVICE_SERVICE_H
#define DEVICE_SERVICE_H

#include "device_info.h"

#include <stdbool.h>

void device_service_init(void);

/** Current identity; never NULL after device_service_init(). */
const device_info_t *device_service_info(void);

/** True when the identity is valid (serial present and persisted in RAM). */
bool device_service_ready(void);

/**
 * Per-device storage key derived from the 96-bit MCU UID and a domain label.
 * Used to obfuscate WiFi credentials at rest. Available after
 * device_service_init(); never NULL.
 */
void device_service_storage_key(uint8_t out[32]);

#endif /* DEVICE_SERVICE_H */
