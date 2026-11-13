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
#include "device_service.h"
#include "selftest_service.h"
#include "fault_service.h"
#include "crash_service.h"
#include "resource_monitor.h"
#include "bsp_time.h"
#include "stm32c562xx.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    host_link_send_json(
        "{\"type\":\"cli\",\"ok\":false,"
        "\"commands\":[\"help\",\"version\",\"part\",\"wifi\",\"sensor\","
        "\"ota start\",\"ota status\",\"ota abort\",\"log\",\"crash\","
        "\"crash clear\",\"resource\",\"device\",\"serial\","
        "\"serial set <sn>\",\"selftest\",\"factory test\","
        "\"factory reset\"]}");
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

static void cmd_log(void)
{
    char json[850];
    if (fault_service_json(json, sizeof(json)) == 0u) {
        host_link_send_json(
            "{\"type\":\"cli\",\"ok\":false,\"error\":\"log buffer too small\"}");
        return;
    }
    host_link_send_json(json);
}

static void cmd_crash(void)
{
    static char record_json[768];
    static char line[800];
    char head[96];

    (void)snprintf(head, sizeof(head),
                   "{\"type\":\"crash\",\"count\":%u,\"capacity\":%u}",
                   (unsigned)crash_service_count(),
                   (unsigned)crash_service_capacity());
    host_link_send_json(head);

    uint16_t count = crash_service_count();
    for (uint16_t i = 0; i < count; ++i) {
        crash_record_t record;
        if (!crash_service_get(i, &record)) {
            continue;
        }
        if (crash_record_to_json(&record, record_json, sizeof(record_json)) == 0u) {
            continue;
        }
        (void)snprintf(line, sizeof(line), "{\"type\":\"crash\",\"record\":%s}",
                       record_json);
        host_link_send_json(line);
    }
}

static void cmd_crash_clear(void)
{
    bool ok = crash_service_clear() == 0;
    host_link_send_json(ok ? "{\"type\":\"cli\",\"cmd\":\"crash\","
                             "\"action\":\"clear\",\"ok\":true}"
                           : "{\"type\":\"cli\",\"cmd\":\"crash\","
                             "\"action\":\"clear\",\"ok\":false}");
}

static void cmd_resource(void)
{
    char json[360];
    if (resource_monitor_json(json, sizeof(json)) == 0u) {
        host_link_send_json(
            "{\"type\":\"cli\",\"ok\":false,\"error\":\"resource unavailable\"}");
        return;
    }
    host_link_send_json(json);
}

static void cmd_device(void)
{
    const device_info_t *info = device_service_info();
    uint32_t version = ota_service_running_version();
    char json[320];
    (void)snprintf(json, sizeof(json),
                   "{\"type\":\"cli\",\"cmd\":\"device\",\"serial\":\"%s\","
                   "\"hardware_id\":%u,\"firmware\":\"%u.%u.%u\","
                   "\"version_raw\":%u,\"provisioned\":%s}",
                   info->serial, (unsigned)info->hardware_id,
                   (unsigned)((version >> 16) & 0xFFu),
                   (unsigned)((version >> 8) & 0xFFu),
                   (unsigned)(version & 0xFFu), (unsigned)version,
                   info->provisioned ? "true" : "false");
    host_link_send_json(json);
}

static void cmd_serial(void)
{
    const device_info_t *info = device_service_info();
    char json[200];
    (void)snprintf(json, sizeof(json),
                   "{\"type\":\"cli\",\"cmd\":\"serial\",\"serial\":\"%s\","
                   "\"hardware_id\":%u,\"provisioned\":%s}",
                   info->serial, (unsigned)info->hardware_id,
                   info->provisioned ? "true" : "false");
    host_link_send_json(json);
}

/* Case-insensitive whole-word match at the start of @p text. */
static bool match_word_ci(const char *text, uint32_t length, const char *word)
{
    uint32_t n = (uint32_t)strlen(word);
    if (length < n || (length > n && text[n] != ' ' && text[n] != '\t')) {
        return false;
    }
    for (uint32_t i = 0; i < n; ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != word[i]) {
            return false;
        }
    }
    return true;
}

static void cmd_serial_set(const char *line, uint32_t length)
{
    uint32_t i = 0u;
    while (i < length && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (!match_word_ci(line + i, length - i, "serial")) {
        host_link_send_json("{\"type\":\"cli\",\"cmd\":\"serial\","
                            "\"ok\":false,\"error\":\"bad syntax\"}");
        return;
    }
    i += 6u;
    while (i < length && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (!match_word_ci(line + i, length - i, "set")) {
        host_link_send_json("{\"type\":\"cli\",\"cmd\":\"serial\","
                            "\"ok\":false,\"error\":\"bad syntax\"}");
        return;
    }
    i += 3u;
    while (i < length && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }

    char serial[DEVICE_SERIAL_MAX + 1u];
    uint32_t len = 0u;
    while (i < length && line[i] != ' ' && line[i] != '\t' &&
           line[i] != '\r' && line[i] != '\n') {
        if (len >= DEVICE_SERIAL_MAX) {
            len = 0u;
            break;
        }
        serial[len++] = line[i++];
    }
    serial[len] = '\0';

    int rc = (len == 0u) ? -1 : device_service_set_serial(serial);
    if (rc == 0) {
        const device_info_t *info = device_service_info();
        char json[220];
        (void)snprintf(json, sizeof(json),
                       "{\"type\":\"cli\",\"cmd\":\"serial\",\"ok\":true,"
                       "\"serial\":\"%s\",\"hardware_id\":%u}",
                       info->serial, (unsigned)info->hardware_id);
        host_link_send_json(json);
    } else {
        host_link_send_json(rc == -2
            ? "{\"type\":\"cli\",\"cmd\":\"serial\",\"ok\":false,"
              "\"error\":\"flash write failed\"}"
            : "{\"type\":\"cli\",\"cmd\":\"serial\",\"ok\":false,"
              "\"error\":\"invalid serial\"}");
    }
}

static void cmd_factory_test(void)
{
    factory_result_t result;
    factory_service_run(&result);
    const device_info_t *info = device_service_info();
    uint32_t version = ota_service_running_version();
    char json[440];
    (void)snprintf(json, sizeof(json),
                   "{\"type\":\"cli\",\"cmd\":\"factory\",\"action\":\"test\","
                   "\"ok\":%s,\"serial\":\"%s\",\"hardware_id\":%u,"
                   "\"firmware\":\"%u.%u.%u\",\"uptime_ms\":%u,"
                   "\"flash\":%s,\"i2c\":%s,\"adc\":%s,\"ble\":%s,"
                   "\"serial_ok\":%s,\"crash_clean\":%s,\"crash_count\":%u,"
                   "\"failed_mask\":%u}",
                   result.all_ok ? "true" : "false", info->serial,
                   (unsigned)info->hardware_id,
                   (unsigned)((version >> 16) & 0xFFu),
                   (unsigned)((version >> 8) & 0xFFu),
                   (unsigned)(version & 0xFFu), (unsigned)bsp_time_ms(),
                   result.selftest.flash_ok ? "true" : "false",
                   result.selftest.i2c_ok ? "true" : "false",
                   result.selftest.adc_ok ? "true" : "false",
                   result.selftest.ble_ok ? "true" : "false",
                   result.serial_ok ? "true" : "false",
                   result.crash_clean ? "true" : "false",
                   (unsigned)crash_service_count(),
                   (unsigned)result.failed_mask);
    host_link_send_json(json);
}

static void cmd_selftest(void)
{
    selftest_result_t result;
    selftest_service_run(&result);
    char json[240];
    (void)snprintf(json, sizeof(json),
                   "{\"type\":\"cli\",\"cmd\":\"selftest\",\"ok\":%s,"
                   "\"flash\":%s,\"i2c\":%s,\"adc\":%s,\"ble\":%s,"
                   "\"failed_mask\":%u}",
                   result.all_ok ? "true" : "false",
                   result.flash_ok ? "true" : "false",
                   result.i2c_ok ? "true" : "false",
                   result.adc_ok ? "true" : "false",
                   result.ble_ok ? "true" : "false",
                   (unsigned)result.failed_mask);
    host_link_send_json(json);
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
    case CLI_LOG:           cmd_log(); break;
    case CLI_CRASH:         cmd_crash(); break;
    case CLI_CRASH_CLEAR:   cmd_crash_clear(); break;
    case CLI_RESOURCE:      cmd_resource(); break;
    case CLI_DEVICE:        cmd_device(); break;
    case CLI_SERIAL:        cmd_serial(); break;
    case CLI_SERIAL_SET:    cmd_serial_set(line, length); break;
    case CLI_SELFTEST:      cmd_selftest(); break;
    case CLI_FACTORY_TEST:  cmd_factory_test(); break;
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
