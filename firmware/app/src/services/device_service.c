// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "device_service.h"
#include "bsp_flash.h"
#include "flash_layout.h"
#include "app_config.h"
#include "fault_service.h"
#include "stm32c562xx.h"

#include <string.h>

static record_store_t g_store;
static device_info_t g_info;
static bool g_ready;

static int dev_erase(void *context, uint32_t address)
{
    (void)context;
    return bsp_flash_erase_page(address);
}

static int dev_program(void *context, uint32_t address, const void *data,
                       uint32_t length)
{
    (void)context;
    return bsp_flash_program(address, data, length);
}

static int dev_read(void *context, uint32_t address, void *out, uint32_t length)
{
    (void)context;
    return bsp_flash_read(address, out, length);
}

void device_service_init(void)
{
    record_flash_if_t iface;
    iface.context = NULL;
    iface.erase_page = dev_erase;
    iface.program = dev_program;
    iface.read = dev_read;

    g_ready = false;
    device_info_defaults(&g_info, APP_HARDWARE_ID);

    if (record_store_init(&g_store, &iface, FLASH_DEVICE_PAGE0, FLASH_DEVICE_PAGE1,
                          (uint16_t)sizeof(device_info_record_t)) == 0) {
        if (device_info_load(&g_store, &g_info) == 0) {
            g_ready = true;
            return;
        }
    }

    /* First boot or lost record: derive a stable serial from the MCU UID. */
    const uint8_t *uid = (const uint8_t *)(uintptr_t)UID_BASE;
    device_info_defaults(&g_info, APP_HARDWARE_ID);
    device_info_build_serial(uid, g_info.serial);
    g_info.provisioned = true;

    if (!device_info_valid(&g_info)) {
        fault_service_raise(ERR_CODE_STORE_CORRUPT, 0u);
        return;
    }
    g_ready = true;
    if (device_info_save(&g_store, &g_info) != 0) {
        /* RAM identity is still usable for this boot; retried next boot. */
        fault_service_raise(ERR_CODE_FLASH_PROGRAM, 0u);
    }
}

const device_info_t *device_service_info(void)
{
    return &g_info;
}

bool device_service_ready(void)
{
    return g_ready;
}
