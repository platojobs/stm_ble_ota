// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    main.c
 * @brief   Application entry point: wiring of services and the main loop.
 *
 * Boot flow:
 *   bootloader -> validates slot A -> jumps here at 0x08008000.
 *
 * Runtime flow:
 *   USART1 <-> ESP32 AT (WiFi + BLE), USART2 <-> PC test tool,
 *   I2C1 <-> SHT30, ADC1_IN0 <-> light sensor.
 *   Sensor data is sampled every second, filtered, sent over BLE char 1 and
 *   mirrored as JSON on the PC link. OTA frames from either transport go
 *   through the same parser and receiver; on completion the device resets into
 *   the bootloader, which installs slot B into slot A.
 */

#include "stm32c562xx.h"
#include "app_config.h"
#include "bsp_time.h"
#include "bsp_uart.h"
#include "bsp_gpio.h"
#include "sensor_service.h"
#include "wifi_service.h"
#include "ble_service.h"
#include "ota_service.h"
#include "host_link.h"
#include "cli_service.h"
#include "esp32_at.h"
#include "mini_json.h"
#include "config_store.h"
#include "bsp_watchdog.h"

#include <stdio.h>
#include <string.h>

static esp_at_t g_esp;
static uint32_t g_info_due_ms;
static bool g_info_pending;
static uint32_t g_led_last_ms;
static bool g_led_state;

static void send_info_json(void);

static void build_info_json(char *out, size_t out_size)
{
    uint32_t version = ota_service_running_version();
    uint8_t major = (uint8_t)((version >> 16) & 0xFFu);
    uint8_t minor = (uint8_t)((version >> 8) & 0xFFu);
    uint8_t patch = (uint8_t)(version & 0xFFu);

    (void)snprintf(out, out_size,
                   "{\"type\":\"info\",\"name\":\"%s\",\"version\":\"%u.%u.%u\","
                   "\"version_raw\":%u,\"hardware_id\":%u,\"ota_state\":%u,"
                   "\"last_reason\":%u,\"ble_connected\":%s,\"wifi_connected\":%s}",
                   APP_NAME, (unsigned)major, (unsigned)minor, (unsigned)patch,
                   (unsigned)version, (unsigned)APP_HARDWARE_ID,
                   (unsigned)ota_service_state(), (unsigned)ota_service_last_reason(),
                   ble_service_connected() ? "true" : "false",
                   wifi_service_connected() ? "true" : "false");
}

static void send_info_json(void)
{
    char json[220];
    build_info_json(json, sizeof(json));
    host_link_send_json(json);
    if (ble_service_connected()) {
        (void)ble_service_notify_info(json);
    }
}

static void on_wifi_reply(const char *json, void *context)
{
    (void)context;
    host_link_send_json(json);
    if (ble_service_connected()) {
        (void)ble_service_notify_provision(json);
    }
}

static void on_sensor_report(const sensor_notify_payload_t *payload, void *context)
{
    (void)context;
    if (ble_service_connected()) {
        (void)ble_service_notify_sensor(payload);
    }

    char json[200];
    (void)snprintf(json, sizeof(json),
                   "{\"type\":\"sensor\",\"valid\":%u,\"uptime_ms\":%u,"
                   "\"temperature_centi\":%d,\"humidity_centi\":%d,\"light_mv\":%u}",
                   (unsigned)payload->valid_mask, (unsigned)payload->uptime_ms,
                   (int)payload->temperature_centi, (int)payload->humidity_centi,
                   (unsigned)payload->light_mv);
    host_link_send_json(json);
}

static void on_ota_notify(const uint8_t *frame, uint32_t length, void *context)
{
    (void)context;
    if (ble_service_connected()) {
        (void)ble_service_notify_ota(frame, length);
    }

    /* Mirror progress to the PC link as JSON for easy tooling. */
    uint16_t payload_len;
    memcpy(&payload_len, frame + 6u, sizeof(payload_len));
    if ((frame[2] == STM_FRAME_OTA_PROGRESS || frame[2] == STM_FRAME_OTA_RESULT) &&
        length >= STM_FRAME_HEADER_SIZE + sizeof(ota_progress_payload_t) &&
        payload_len >= sizeof(ota_progress_payload_t)) {
        ota_progress_payload_t progress;
        memcpy(&progress, frame + STM_FRAME_HEADER_SIZE, sizeof(progress));
        char json[160];
        (void)snprintf(json, sizeof(json),
                       "{\"type\":\"ota\",\"state\":%u,\"reason\":%u,"
                       "\"received\":%u,\"total\":%u}",
                       (unsigned)progress.state, (unsigned)progress.reason,
                       (unsigned)progress.received, (unsigned)progress.total);
        host_link_send_json(json);
    }
}

static void on_provision_text(const uint8_t *data, uint32_t length, void *context)
{
    (void)context;
    char json[WIFI_SSID_MAX + WIFI_PASSWORD_MAX + 64u];
    if (length >= sizeof(json)) {
        return;
    }
    memcpy(json, data, length);
    json[length] = '\0';
    (void)wifi_service_handle_command(json, length);
}

static void on_host_json(const char *json, void *context)
{
    (void)context;
    /* Lines that do not start with '{' are CLI commands. */
    if (json[0] != '{') {
        cli_service_handle_line(json, (uint32_t)strlen(json));
        return;
    }
    char command[24];
    if (json_get_string(json, "cmd", command, sizeof(command)) != JSON_OK) {
        host_link_send_json("{\"type\":\"error\",\"message\":\"missing cmd\"}");
        return;
    }
    if (strcmp(command, "info") == 0) {
        send_info_json();
        return;
    }
    if (strncmp(command, "wifi.", 5u) == 0) {
        (void)wifi_service_handle_command(json, (uint32_t)strlen(json));
        return;
    }
    host_link_send_json("{\"type\":\"error\",\"message\":\"unknown command\"}");
}

static void on_host_frame(const stm_frame_t *frame, void *context)
{
    (void)context;
    if (frame == NULL) {
        return;
    }
    if (frame->type >= STM_FRAME_OTA_BEGIN && frame->type <= STM_FRAME_OTA_ABORT) {
        ota_service_on_frame(frame);
    } else if (frame->type == STM_FRAME_CMD_JSON && frame->payload_len > 0u) {
        char json[192];
        uint16_t length = frame->payload_len;
        if (length >= sizeof(json)) {
            length = (uint16_t)(sizeof(json) - 1u);
        }
        memcpy(json, frame->payload, length);
        json[length] = '\0';
        on_host_json(json, NULL);
    }
}

static void on_ota_bytes(const uint8_t *data, uint32_t length, void *context)
{
    (void)context;
    ota_service_on_bytes(data, length);
}

static void on_esp_urc(const esp_at_event_t *event, void *context)
{
    (void)context;
    if (event == NULL) {
        return;
    }
    ble_service_on_urc(event, NULL);
    wifi_service_on_urc(event, NULL);

    if (event->type == ESP_AT_EVT_BLE_CONNECTED) {
        /* The central needs a moment to subscribe before the first notify. */
        g_info_pending = true;
        g_info_due_ms = bsp_time_ms() + 200u;
    } else if (event->type == ESP_AT_EVT_BLE_DISCONNECTED) {
        ota_service_on_ble_disconnect();
    }
}

int main(void)
{
    bsp_time_init();
    bsp_led_init();
    bsp_button_init();
    bsp_uart_init();

    esp_at_init(&g_esp, BSP_UART_ESP, on_esp_urc, NULL);

    if (ota_service_init() != 0) {
        for (;;) {
            bsp_led_toggle();
            bsp_delay_ms(100u);
        }
    }

    (void)wifi_service_init(&g_esp);
    wifi_service_set_reply_callback(on_wifi_reply, NULL);

    (void)ble_service_init(&g_esp);
    ble_service_set_provision_handler(on_provision_text, NULL);
    ble_service_set_ota_handler(on_ota_bytes, NULL);

    sensor_service_init();
    sensor_service_set_report_callback(on_sensor_report, NULL);

    ota_service_set_notify_callback(on_ota_notify, NULL);

    host_link_init(on_host_json, on_host_frame, NULL);
    g_led_last_ms = bsp_time_ms();

    /* Bootloader-started IWDG is already running; re-arm it. The boot is only
     * confirmed after a grace period, so a reset loop caused by an early
     * crash or hang still reaches the bootloader's rollback threshold. */
    bsp_watchdog_init();
    uint32_t boot_grace_start = bsp_time_ms();
    bool boot_confirmed = false;

    for (;;) {
        bsp_watchdog_feed();
        host_link_poll();
        esp_at_poll(&g_esp);

        uint32_t now = bsp_time_ms();
        if (!boot_confirmed && (now - boot_grace_start) >= 3000u) {
            boot_confirmed = true;
            ota_service_confirm_boot();
        }
        wifi_service_tick(now);
        sensor_service_tick(now);
        ota_service_tick(now);

        if (g_info_pending && ble_service_connected() &&
            (int32_t)(now - g_info_due_ms) >= 0) {
            g_info_pending = false;
            send_info_json();
        }

        if ((now - g_led_last_ms) >= 500u) {
            g_led_last_ms = now;
            g_led_state = !g_led_state;
            bsp_led_set(g_led_state);
        }
    }
}
