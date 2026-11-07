// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "cli_service.h"
#include "cli_parser.h"
#include "host_link.h"
#include "app_config.h"
#include "flash_layout.h"
#include "ota_service.h"
#include "wifi_service.h"
#include "sensor_service.h"
#include "ble_service.h"
#include "bsp_time.h"
#include "stm32c562xx.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    host_link_send_json(
        "{\"type\":\"cli\",\"ok\":false,"
        "\"commands\":[\"help\",\"version\",\"part\",\"wifi\",\"sensor\","
        "\"ota start\",\"ota status\",\"ota abort\",\"factory reset\"]}");
}

static void cmd_help(void)
{
    usage();
}

static void cmd_version(void)
{
    uint32_t version = ota_service_running_version();
    uint8_t major = (uint8_t)((version >> 16) & 0xFFu);
    uint8_t minor = (uint8_t)((version >> 8) & 0xFFu);
    uint8_t patch = (uint8_t)(version & 0xFFu);
    char json[160];
    (void)snprintf(json, sizeof(json),
                   "{\"type\":\"cli\",\"cmd\":\"version\",\"name\":\"%s\","
                   "\"version\":\"%u.%u.%u\",\"version_raw\":%u,"
                   "\"build_id\":%u,\"hardware_id\":%u}",
                   APP_NAME, (unsigned)major, (unsigned)minor, (unsigned)patch,
                   (unsigned)version, (unsigned)APP_BUILD_ID,
                   (unsigned)APP_HARDWARE_ID);
    host_link_send_json(json);
}

static void cmd_part(void)
{
    char json[360];
    (void)snprintf(json, sizeof(json),
                   "{\"type\":\"cli\",\"cmd\":\"part\","
                   "\"boot\":{\"start\":%u,\"size\":%u},"
                   "\"slot_a\":{\"start\":%u,\"size\":%u},"
                   "\"slot_b\":{\"start\":%u,\"size\":%u},"
                   "\"backup\":{\"start\":%u,\"size\":%u},"
                   "\"meta\":{\"start\":%u,\"size\":%u},"
                   "\"config\":{\"start\":%u,\"size\":%u}}",
                   (unsigned)FLASH_BOOT_START, (unsigned)FLASH_BOOT_SIZE,
                   (unsigned)FLASH_SLOT_A_START, (unsigned)FLASH_SLOT_A_SIZE,
                   (unsigned)FLASH_SLOT_B_START, (unsigned)FLASH_SLOT_B_SIZE,
                   (unsigned)FLASH_BACKUP_START, (unsigned)FLASH_BACKUP_SIZE,
                   (unsigned)FLASH_META_PAGE0, (unsigned)FLASH_META_STORE_SIZE,
                   (unsigned)FLASH_CONFIG_PAGE0,
                   (unsigned)FLASH_CONFIG_STORE_SIZE);
    host_link_send_json(json);
}

static void cmd_wifi(void)
{
    char json[220];
    wifi_service_status_json(json, sizeof(json));
    host_link_send_json(json);
}

static void cmd_sensor(void)
{
    const sensor_notify_payload_t *p = sensor_service_last();
    char json[180];
    if (p == NULL) {
        host_link_send_json("{\"type\":\"cli\",\"cmd\":\"sensor\",\"valid\":0}");
        return;
    }
    (void)snprintf(json, sizeof(json),
                   "{\"type\":\"cli\",\"cmd\":\"sensor\",\"valid\":%u,"
                   "\"uptime_ms\":%u,\"temperature_centi\":%d,"
                   "\"humidity_centi\":%d,\"light_mv\":%u}",
                   (unsigned)p->valid_mask, (unsigned)p->uptime_ms,
                   (int)p->temperature_centi, (int)p->humidity_centi,
                   (unsigned)p->light_mv);
    host_link_send_json(json);
}

static void cmd_ota_start(void)
{
    ota_service_abort_transfer();
    host_link_send_json(
        "{\"type\":\"cli\",\"cmd\":\"ota\",\"action\":\"start\",\"ready\":true}");
}

static void cmd_ota_status(void)
{
    err_code_t code = ota_service_last_error_code();
    char json[240];
    (void)snprintf(json, sizeof(json),
                   "{\"type\":\"cli\",\"cmd\":\"ota\",\"action\":\"status\","
                   "\"state\":%u,\"reason\":%u,\"err_code\":%u,"
                   "\"err_name\":\"%s\",\"received\":%u,\"total\":%u}",
                   (unsigned)ota_service_state(),
                   (unsigned)ota_service_last_reason(),
                   (unsigned)code, err_code_name(code),
                   (unsigned)ota_service_received(),
                   (unsigned)ota_service_total());
    host_link_send_json(json);
}

static void cmd_ota_abort(void)
{
    ota_service_abort_transfer();
    host_link_send_json(
        "{\"type\":\"cli\",\"cmd\":\"ota\",\"action\":\"abort\",\"ok\":true}");
}

static void cmd_factory_reset(void)
{
    host_link_send_json(
        "{\"type\":\"cli\",\"cmd\":\"factory\",\"reset\":true}");
    /* Wipe provisioning and OTA metadata, then reboot into the bootloader. */
    (void)wifi_service_handle_command("{\"cmd\":\"wifi.clear\"}", 19u);
    ota_service_factory_reset();
    bsp_delay_ms(50u);
    NVIC_SystemReset();
}

void cli_service_handle_line(const char *line, uint32_t length)
{
    if (line == NULL) {
        usage();
        return;
    }

    cli_command_t cmd = cli_parse(line, length);
    switch (cmd) {
    case CLI_HELP:          cmd_help(); break;
    case CLI_VERSION:       cmd_version(); break;
    case CLI_PART:          cmd_part(); break;
    case CLI_WIFI:          cmd_wifi(); break;
    case CLI_SENSOR:        cmd_sensor(); break;
    case CLI_OTA_START:     cmd_ota_start(); break;
    case CLI_OTA_STATUS:    cmd_ota_status(); break;
    case CLI_OTA_ABORT:     cmd_ota_abort(); break;
    case CLI_FACTORY_RESET: cmd_factory_reset(); break;
    case CLI_TOO_LONG:
        host_link_send_json(
            "{\"type\":\"cli\",\"ok\":false,\"error\":\"line too long\"}");
        break;
    case CLI_UNKNOWN:
    default:
        usage();
        break;
    }
}
