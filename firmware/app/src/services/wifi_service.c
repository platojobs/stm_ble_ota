// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    wifi_service.c
 * @brief   WiFi provisioning state machine for the ESP32 AT co-processor.
 */

#include "wifi_service.h"
#include "config_store.h"
#include "mini_json.h"
#include "bsp_flash.h"
#include "bsp_time.h"
#include "flash_layout.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>

#define RECONNECT_INTERVAL_MS 30000u
#define ESP_BOOT_ATTEMPTS     5u

static esp_at_t *g_at;
static record_store_t g_store;
static wifi_config_t g_config;
static bool g_connected;
static bool g_esp_ready;
static uint32_t g_last_attempt_ms;
static wifi_reply_cb_t g_reply_cb;
static void *g_reply_context;

static int record_erase(void *context, uint32_t page_address)
{
    (void)context;
    return bsp_flash_erase_page(page_address);
}

static int record_program(void *context, uint32_t address, const void *data,
                          uint32_t length)
{
    (void)context;
    return bsp_flash_program(address, data, length);
}

static int record_read(void *context, uint32_t address, void *out, uint32_t length)
{
    (void)context;
    return bsp_flash_read(address, out, length);
}

static void reply(const char *json)
{
    if (g_reply_cb != NULL) {
        g_reply_cb(json, g_reply_context);
    }
}

static void reply_error(const char *message)
{
    char json[160];
    char escaped[96];
    (void)json_escape_string(escaped, sizeof(escaped), message);
    (void)snprintf(json, sizeof(json),
                   "{\"type\":\"wifi\",\"ok\":false,\"error\":\"%s\"}", escaped);
    reply(json);
}

static bool credentials_safe(const char *text)
{
    /* AT+CWJAP is quote/comma separated; reject characters that break it. */
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == '"' || *p == '\n' || *p == '\r') {
            return false;
        }
    }
    return true;
}

static int esp_wait_ready(void)
{
    for (uint32_t attempt = 0; attempt < ESP_BOOT_ATTEMPTS; ++attempt) {
        if (esp_at_exec(g_at, "AT", 1000u, false) == ESP_AT_EXEC_OK) {
            return 0;
        }
        bsp_delay_ms(500u);
    }
    return -1;
}

static int esp_configure(void)
{
    if (esp_at_exec(g_at, "ATE0", ESP_COMMAND_TIMEOUT_MS, false) != ESP_AT_EXEC_OK) {
        return -1;
    }
    if (esp_at_exec(g_at, "AT+CWMODE=1", ESP_COMMAND_TIMEOUT_MS, false) != ESP_AT_EXEC_OK) {
        return -1;
    }
    /* Persist the ESP's own auto-connect choice as well. */
    (void)esp_at_exec(g_at, "AT+CWAUTOCONN=1", ESP_COMMAND_TIMEOUT_MS, false);
    return 0;
}

static int connect_wifi(const char *ssid, const char *password)
{
    if (!credentials_safe(ssid) || !credentials_safe(password)) {
        return -1;
    }

    char command[160];
    (void)snprintf(command, sizeof(command), "AT+CWJAP=\"%s\",\"%s\"", ssid,
                   password);
    if (esp_at_exec(g_at, command, WIFI_CONNECT_TIMEOUT_MS, false) != ESP_AT_EXEC_OK) {
        g_connected = false;
        return -1;
    }
    g_connected = true;
    return 0;
}

static void connected_ssid_json(char *out, size_t out_size)
{
    char escaped[WIFI_SSID_MAX + 1u];
    (void)json_escape_string(escaped, sizeof(escaped), g_config.ssid);
    (void)snprintf(out, out_size,
                   "{\"type\":\"wifi\",\"ok\":true,\"connected\":%s,"
                   "\"ssid\":\"%s\",\"provisioned\":%s}",
                   g_connected ? "true" : "false", escaped,
                   g_config.provisioned ? "true" : "false");
}

void wifi_service_status_json(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0u) {
        return;
    }

    char ip[40] = "";
    if (g_connected && g_esp_ready) {
        if (esp_at_exec(g_at, "AT+CIPSTA?", ESP_COMMAND_TIMEOUT_MS, false) ==
            ESP_AT_EXEC_OK) {
            const char *marker = strstr(esp_at_last_response(g_at), "+CIPSTA:ip:\"");
            if (marker != NULL) {
                marker += strlen("+CIPSTA:ip:\"");
                const char *end = strchr(marker, '"');
                if (end != NULL && (size_t)(end - marker) < sizeof(ip)) {
                    memcpy(ip, marker, (size_t)(end - marker));
                    ip[end - marker] = '\0';
                }
            }
        }
    }

    char escaped_ssid[WIFI_SSID_MAX + 1u];
    char escaped_ip[64];
    (void)json_escape_string(escaped_ssid, sizeof(escaped_ssid), g_config.ssid);
    (void)json_escape_string(escaped_ip, sizeof(escaped_ip), ip);
    (void)snprintf(out, out_size,
                   "{\"type\":\"wifi\",\"ok\":true,\"connected\":%s,"
                   "\"ssid\":\"%s\",\"ip\":\"%s\",\"provisioned\":%s}",
                   g_connected ? "true" : "false", escaped_ssid, escaped_ip,
                   g_config.provisioned ? "true" : "false");
}

int wifi_service_init(esp_at_t *at)
{
    g_at = at;
    memset(&g_config, 0, sizeof(g_config));

    record_flash_if_t iface = {
        .context = NULL,
        .erase_page = record_erase,
        .program = record_program,
        .read = record_read,
    };
    if (record_store_init(&g_store, &iface, FLASH_CONFIG_PAGE0, FLASH_CONFIG_PAGE1,
                          sizeof(wifi_config_record_t)) != 0) {
        return -1;
    }

    (void)wifi_config_load(&g_store, &g_config);

    if (esp_wait_ready() != 0) {
        return -1;
    }
    if (esp_configure() != 0) {
        return -1;
    }
    g_esp_ready = true;

    /* The first tick performs the boot reconnect, after BLE has been set up. */
    g_last_attempt_ms = bsp_time_ms() - RECONNECT_INTERVAL_MS;
    return 0;
}

void wifi_service_set_reply_callback(wifi_reply_cb_t callback, void *context)
{
    g_reply_cb = callback;
    g_reply_context = context;
}

static int handle_set(const char *json)
{
    char ssid[WIFI_SSID_MAX + 1u];
    char password[WIFI_PASSWORD_MAX + 1u];

    if (json_get_string(json, "ssid", ssid, sizeof(ssid)) != JSON_OK ||
        ssid[0] == '\0') {
        reply_error("missing or invalid ssid");
        return -1;
    }
    if (json_get_string(json, "password", password, sizeof(password)) != JSON_OK &&
        json_get_string(json, "pass", password, sizeof(password)) != JSON_OK) {
        reply_error("missing password");
        return -1;
    }

    if (connect_wifi(ssid, password) != 0) {
        reply_error("wifi connection failed");
        return -1;
    }

    wifi_config_t next;
    memset(&next, 0, sizeof(next));
    memcpy(next.ssid, ssid, strlen(ssid) + 1u);
    memcpy(next.password, password, strlen(password) + 1u);
    next.auto_connect = 1u;
    next.provisioned = 1u;

    if (wifi_config_save(&g_store, &next) != 0) {
        reply_error("could not persist credentials");
        return -1;
    }
    g_config = next;

    char response[192];
    connected_ssid_json(response, sizeof(response));
    reply(response);
    return 0;
}

static int handle_clear(void)
{
    (void)esp_at_exec(g_at, "AT+CWQAP", ESP_COMMAND_TIMEOUT_MS, false);
    g_connected = false;
    memset(&g_config, 0, sizeof(g_config));
    (void)wifi_config_clear(&g_store);

    char response[192];
    connected_ssid_json(response, sizeof(response));
    reply(response);
    return 0;
}

int wifi_service_handle_command(const char *json, uint32_t length)
{
    (void)length;
    if (json == NULL) {
        return -1;
    }
    if (g_at == NULL || !g_esp_ready) {
        reply_error("esp32 not ready");
        return -1;
    }

    char command[24];
    if (json_get_string(json, "cmd", command, sizeof(command)) != JSON_OK) {
        reply_error("missing cmd");
        return -1;
    }

    if (strcmp(command, "wifi.set") == 0) {
        return handle_set(json);
    }
    if (strcmp(command, "wifi.clear") == 0) {
        return handle_clear();
    }
    if (strcmp(command, "wifi.status") == 0) {
        char response[192];
        wifi_service_status_json(response, sizeof(response));
        reply(response);
        return 0;
    }

    reply_error("unknown wifi command");
    return -1;
}

void wifi_service_on_urc(const esp_at_event_t *event, void *context)
{
    (void)context;
    if (event == NULL) {
        return;
    }
    switch (event->type) {
    case ESP_AT_EVT_WIFI_GOT_IP:
        g_connected = true;
        break;
    case ESP_AT_EVT_WIFI_DISCONNECTED:
        g_connected = false;
        break;
    default:
        break;
    }
}

void wifi_service_tick(uint32_t now_ms)
{
    if (!g_esp_ready || !g_config.provisioned || !g_config.auto_connect ||
        g_connected) {
        return;
    }
    if ((now_ms - g_last_attempt_ms) < RECONNECT_INTERVAL_MS) {
        return;
    }
    g_last_attempt_ms = now_ms;
    (void)connect_wifi(g_config.ssid, g_config.password);
}

bool wifi_service_connected(void)
{
    return g_connected;
}

const char *wifi_service_ssid(void)
{
    return g_config.ssid;
}
