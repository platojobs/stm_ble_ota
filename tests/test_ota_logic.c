// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "test_framework.h"
#include "fake_flash.h"
#include "flash_layout.h"
#include "crc.h"
#include "record_store.h"
#include "config_store.h"
#include "ota_metadata.h"
#include "ota_receiver.h"
#include "boot_install.h"
#include "image_check.h"
#include "sha256.h"
#include "device_info.h"

#include <string.h>

/* A 512 KB fake flash covering the whole STM32C562RE program memory. */
static uint8_t g_storage[FLASH_TOTAL_SIZE];
static fake_flash_t g_flash;

static void flash_setup(void)
{
    fake_flash_init(&g_flash, g_storage, FLASH_TOTAL_SIZE, FLASH_BOOT_START);
}

/* --- adapters ------------------------------------------------------------ */

static record_flash_if_t make_record_if(void)
{
    record_flash_if_t iface;
    iface.context = &g_flash;
    iface.erase_page = fake_flash_erase;
    iface.program = fake_flash_program;
    iface.read = fake_flash_read;
    return iface;
}

static image_flash_if_t make_image_if(void)
{
    image_flash_if_t iface;
    iface.context = &g_flash;
    iface.read = fake_flash_read;
    return iface;
}

typedef struct {
    fake_flash_t *flash;
    record_flash_if_t iface;
    record_store_t meta_store;
} install_context_t;

/* The record store keeps a pointer to its flash interface, so the interface
 * must outlive the setup function: it lives inside the context. */
static int install_ctx_init(install_context_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->flash = &g_flash;
    ctx->iface = make_record_if();
    return record_store_init(&ctx->meta_store, &ctx->iface,
                             FLASH_META_PAGE0, FLASH_META_PAGE1,
                             (uint16_t)sizeof(ota_metadata_t));
}

/* The injected IO callbacks all receive install_context_t so the fake flash
 * and the metadata record store share one context object. */
static int ctx_erase(void *context, uint32_t page_address)
{
    install_context_t *ctx = (install_context_t *)context;
    return fake_flash_erase(ctx->flash, page_address);
}

static int ctx_program(void *context, uint32_t address, const void *data, uint32_t length)
{
    install_context_t *ctx = (install_context_t *)context;
    return fake_flash_program(ctx->flash, address, data, length);
}

static int ctx_read(void *context, uint32_t address, void *out, uint32_t length)
{
    install_context_t *ctx = (install_context_t *)context;
    return fake_flash_read(ctx->flash, address, out, length);
}

static int boot_metadata_save(void *context, const ota_metadata_t *meta)
{
    install_context_t *ctx = (install_context_t *)context;
    return ota_metadata_write(&ctx->meta_store, meta);
}

static boot_install_io_t make_boot_io(install_context_t *ctx)
{
    boot_install_io_t io;
    io.context = ctx;
    io.flash_erase_page = ctx_erase;
    io.flash_program = ctx_program;
    io.flash_read = ctx_read;
    io.metadata_save = boot_metadata_save;
    return io;
}

typedef struct {
    uint32_t notifications;
    ota_state_t last_state;
    ota_reason_t last_reason;
    uint32_t received;
    uint32_t total;
} notify_stats_t;

typedef struct {
    install_context_t install;
    ota_receiver_io_t io;
    notify_stats_t stats;
} receiver_context_t;

static void receiver_notify(void *context, ota_state_t state, ota_reason_t reason,
                            uint32_t received, uint32_t total, uint32_t crc32)
{
    (void)crc32;
    receiver_context_t *ctx = (receiver_context_t *)context;
    ctx->stats.notifications++;
    ctx->stats.last_state = state;
    ctx->stats.last_reason = reason;
    ctx->stats.received = received;
    ctx->stats.total = total;
}

static int receiver_erase(void *context, uint32_t page_address)
{
    receiver_context_t *ctx = (receiver_context_t *)context;
    return ctx_erase(&ctx->install, page_address);
}

static int receiver_program(void *context, uint32_t address, const void *data,
                            uint32_t length)
{
    receiver_context_t *ctx = (receiver_context_t *)context;
    return ctx_program(&ctx->install, address, data, length);
}

static int receiver_read(void *context, uint32_t address, void *out, uint32_t length)
{
    receiver_context_t *ctx = (receiver_context_t *)context;
    return ctx_read(&ctx->install, address, out, length);
}

static int receiver_metadata_save(void *context, const ota_metadata_t *meta)
{
    receiver_context_t *ctx = (receiver_context_t *)context;
    return boot_metadata_save(&ctx->install, meta);
}

static int receiver_setup(receiver_context_t *ctx, ota_receiver_t *receiver,
                          uint32_t hardware_id)
{
    memset(ctx, 0, sizeof(*ctx));
    if (install_ctx_init(&ctx->install) != 0) {
        return -1;
    }

    ctx->io.context = ctx;
    ctx->io.flash_erase_page = receiver_erase;
    ctx->io.flash_program = receiver_program;
    ctx->io.flash_read = receiver_read;
    ctx->io.metadata_save = receiver_metadata_save;
    ctx->io.notify = receiver_notify;

    return ota_receiver_init(receiver, &ctx->io, FLASH_SLOT_B_START,
                             FLASH_SLOT_B_SIZE, hardware_id);
}

/* --- image helper -------------------------------------------------------- */

/* Fill @p size bytes with a deterministic pattern and a plausible vector
 * table, then return the CRC-32 of the image. */
static uint32_t build_test_image(uint8_t *buffer, uint32_t size, uint32_t base)
{
    memset(buffer, 0x00, size);
    for (uint32_t i = 0; i < size; ++i) {
        buffer[i] = (uint8_t)(i * 7u + 3u);
    }
    /* Initial stack pointer: top of SRAM minus a little. */
    uint32_t sp = SRAM_END - 0x40u;
    /* Reset handler: inside the image, Thumb bit set. */
    uint32_t reset = (base + 0x101u) | 1u;
    memcpy(buffer, &sp, sizeof(sp));
    memcpy(buffer + 4u, &reset, sizeof(reset));
    return crc32_ieee_buf(buffer, size);
}

/* --- record store -------------------------------------------------------- */

TEST_CASE(record_store_roundtrip)
{
    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_META_PAGE0, FLASH_META_PAGE1, 128u), 0);

    uint8_t payload[64];
    for (uint8_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = i;
    }

    uint32_t seq = 0;
    ASSERT_EQ(record_store_save(&store, payload, sizeof(payload), &seq), 0);
    ASSERT_EQ(seq, 1u);

    record_info_t info;
    uint8_t loaded[64];
    ASSERT_EQ(record_store_load(&store, loaded, sizeof(loaded), &info), 0);
    ASSERT_EQ(info.sequence, 1u);
    ASSERT_EQ(memcmp(loaded, payload, sizeof(payload)), 0);

    payload[0] = 0xEEu;
    ASSERT_EQ(record_store_save(&store, payload, sizeof(payload), &seq), 0);
    ASSERT_EQ(seq, 2u);
    ASSERT_EQ(record_store_load(&store, loaded, sizeof(loaded), &info), 0);
    ASSERT_EQ(info.sequence, 2u);
    ASSERT_EQ(loaded[0], 0xEEu);

    /* Alternating pages: second save must have erased the other page. */
    ASSERT_EQ(g_flash.erase_count, 2u);
}

TEST_CASE(record_store_torn_write_keeps_previous)
{
    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_META_PAGE0, FLASH_META_PAGE1, 128u), 0);

    uint8_t payload[64];
    memset(payload, 0xA5, sizeof(payload));
    uint32_t seq = 0;
    ASSERT_EQ(record_store_save(&store, payload, sizeof(payload), &seq), 0);

    /* Kill the second save after its header was written but before the
     * payload completed: the previous record must still load. */
    uint32_t before = g_flash.bytes_programmed;
    g_flash.fail_program_after = (int64_t)(before + 16u);
    ASSERT_EQ(record_store_save(&store, payload, sizeof(payload), &seq), -1);

    g_flash.fail_program_after = -1;
    record_info_t info;
    uint8_t loaded[64];
    ASSERT_EQ(record_store_load(&store, loaded, sizeof(loaded), &info), 0);
    ASSERT_EQ(info.sequence, 1u);
    ASSERT_EQ(loaded[0], 0xA5u);
}

TEST_CASE(record_store_clear_pages)
{
    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_META_PAGE0, FLASH_META_PAGE1, 128u), 0);

    uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ASSERT_EQ(record_store_save(&store, payload, sizeof(payload), NULL), 0);
    ASSERT_EQ(record_store_clear(&store), 0);

    uint8_t loaded[8];
    ASSERT_EQ(record_store_load(&store, loaded, sizeof(loaded), NULL), -1);
}

TEST_CASE(record_store_corrupt_newest_falls_back_to_older)
{
    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_META_PAGE0, FLASH_META_PAGE1, 128u), 0);

    uint8_t older[32];
    uint8_t newer[32];
    memset(older, 0x11, sizeof(older));
    memset(newer, 0x22, sizeof(newer));

    ASSERT_EQ(record_store_save(&store, older, sizeof(older), NULL), 0);
    ASSERT_EQ(record_store_save(&store, newer, sizeof(newer), NULL), 0);

    record_info_t info;
    uint8_t loaded[32];
    ASSERT_EQ(record_store_load(&store, loaded, sizeof(loaded), &info), 0);
    ASSERT_EQ(info.sequence, 2u);
    ASSERT_EQ(loaded[0], 0x22u);

    /* Damage the payload CRC of the newest record only. */
    uint8_t zero = 0x00u;
    ASSERT_EQ(fake_flash_program(&g_flash, info.page + RECORD_HEADER_LEN,
                                 &zero, 1u), 0);
    ASSERT_EQ(record_store_load(&store, loaded, sizeof(loaded), &info), 0);
    ASSERT_EQ(info.sequence, 1u);
    ASSERT_EQ(loaded[0], 0x11u);
}

TEST_CASE(record_store_torn_first_save_leaves_no_record)
{
    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_META_PAGE0, FLASH_META_PAGE1, 128u), 0);

    uint8_t payload[64];
    memset(payload, 0x5A, sizeof(payload));

    /* Header lands, payload write tears: neither page has a valid record. */
    g_flash.fail_program_after = 16u;
    ASSERT_EQ(record_store_save(&store, payload, sizeof(payload), NULL), -1);
    g_flash.fail_program_after = -1;

    uint8_t loaded[64];
    ASSERT_EQ(record_store_load(&store, loaded, sizeof(loaded), NULL), -1);
}

TEST_CASE(record_store_erase_failure_keeps_previous)
{
    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_META_PAGE0, FLASH_META_PAGE1, 128u), 0);

    uint8_t payload[16];
    memset(payload, 0x77, sizeof(payload));
    ASSERT_EQ(record_store_save(&store, payload, sizeof(payload), NULL), 0);

    /* Erase fault on the second save must not destroy the first record. */
    g_flash.fail_erase_after = 0;
    payload[0] = 0x88u;
    ASSERT_EQ(record_store_save(&store, payload, sizeof(payload), NULL), -1);
    g_flash.fail_erase_after = -1;

    uint8_t loaded[16];
    record_info_t info;
    ASSERT_EQ(record_store_load(&store, loaded, sizeof(loaded), &info), 0);
    ASSERT_EQ(info.sequence, 1u);
    ASSERT_EQ(loaded[0], 0x77u);
}

/* --- config store -------------------------------------------------------- */

TEST_CASE(config_store_roundtrip)
{
    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_CONFIG_PAGE0, FLASH_CONFIG_PAGE1, 128u), 0);

    wifi_config_t config;
    memset(&config, 0, sizeof(config));
    strcpy(config.ssid, "stm-ble-ota");
    strcpy(config.password, "s3cret!pass");
    config.auto_connect = 1u;
    config.provisioned = 1u;

    ASSERT_EQ(wifi_config_save(&store, &config), 0);

    wifi_config_t loaded;
    ASSERT_EQ(wifi_config_load(&store, &loaded), 0);
    ASSERT_STREQ(loaded.ssid, "stm-ble-ota");
    ASSERT_STREQ(loaded.password, "s3cret!pass");
    ASSERT_EQ(loaded.auto_connect, 1u);
    ASSERT_EQ(loaded.provisioned, 1u);

    ASSERT_EQ(wifi_config_clear(&store), 0);
    ASSERT_EQ(wifi_config_load(&store, &loaded), -1);
}

TEST_CASE(config_store_rejects_empty)
{
    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_CONFIG_PAGE0, FLASH_CONFIG_PAGE1, 128u), 0);

    wifi_config_t config;
    memset(&config, 0, sizeof(config));
    ASSERT_FALSE(wifi_config_valid(&config));
    ASSERT_EQ(wifi_config_save(&store, &config), -1);
}

TEST_CASE(config_store_corrupt_falls_back_to_unprovisioned)
{
    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_CONFIG_PAGE0, FLASH_CONFIG_PAGE1, 128u), 0);

    wifi_config_t config;
    memset(&config, 0, sizeof(config));
    strcpy(config.ssid, "lab-network");
    strcpy(config.password, "hunter2");
    config.provisioned = 1u;
    ASSERT_EQ(wifi_config_save(&store, &config), 0);

    record_info_t info;
    uint8_t buffer[512];
    ASSERT_EQ(record_store_load(&store, buffer, sizeof(buffer), &info), 0);

    uint8_t byte = 0u;
    ASSERT_EQ(fake_flash_read(&g_flash, info.page + RECORD_HEADER_LEN, &byte, 1u), 0);
    byte = (uint8_t)(byte ^ 0xFFu);
    ASSERT_EQ(fake_flash_program(&g_flash, info.page + RECORD_HEADER_LEN, &byte, 1u), 0);

    /* The service starts from a zeroed struct; a failed load must leave it
     * unprovisioned rather than exposing stale or partial credentials. */
    wifi_config_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    ASSERT_EQ(wifi_config_load(&store, &loaded), -1);
    ASSERT_FALSE(wifi_config_valid(&loaded));
    ASSERT_EQ(loaded.ssid[0], '\0');
}

TEST_CASE(wifi_credentials_validate_lengths_and_chars)
{
    char ssid[WIFI_SSID_MAX + 2u];
    char password[WIFI_PASSWORD_MAX + 2u];

    ASSERT_TRUE(wifi_credentials_valid("lab-network", "hunter2"));
    ASSERT_TRUE(wifi_credentials_valid("lab-network", ""));
    ASSERT_FALSE(wifi_credentials_valid("", "hunter2"));
    ASSERT_FALSE(wifi_credentials_valid(NULL, "hunter2"));
    ASSERT_FALSE(wifi_credentials_valid("lab", NULL));

    memset(ssid, 'a', sizeof(ssid));
    ssid[WIFI_SSID_MAX] = '\0';
    ASSERT_TRUE(wifi_credentials_valid(ssid, "p"));
    ssid[WIFI_SSID_MAX] = 'a';
    ssid[WIFI_SSID_MAX + 1u] = '\0';
    ASSERT_FALSE(wifi_credentials_valid(ssid, "p"));

    memset(password, 'b', sizeof(password));
    password[WIFI_PASSWORD_MAX] = '\0';
    ASSERT_TRUE(wifi_credentials_valid("lab", password));
    password[WIFI_PASSWORD_MAX] = 'b';
    password[WIFI_PASSWORD_MAX + 1u] = '\0';
    ASSERT_FALSE(wifi_credentials_valid("lab", password));

    /* Characters that would break AT+CWJAP quoting or inject CR/LF. */
    ASSERT_FALSE(wifi_credentials_valid("ca\"t", "pass"));
    ASSERT_FALSE(wifi_credentials_valid("lab", "pa\\ss"));
    ASSERT_FALSE(wifi_credentials_valid("lab", "pa\nss"));
    ASSERT_FALSE(wifi_credentials_valid("lab", "p\x01ss"));
    ASSERT_TRUE(wifi_credentials_valid("lab 2.4G", "p@ss!#$%&'()*+,-./"));
}

/* --- device identity ----------------------------------------------------- */

TEST_CASE(device_info_serial_from_uid)
{
    const uint8_t uid[6] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC };
    char serial[DEVICE_SERIAL_MAX + 1u];
    device_info_build_serial(uid, serial);
    ASSERT_STREQ(serial, "C562-123456789ABC");
    ASSERT_EQ((uint32_t)strlen(serial), 17u);

    device_info_build_serial(NULL, serial);
    ASSERT_EQ(serial[0], '\0');
}

TEST_CASE(device_info_validation_and_roundtrip)
{
    device_info_t info;
    device_info_defaults(&info, 0x0050u);
    ASSERT_FALSE(device_info_valid(&info)); /* no serial yet */

    strcpy(info.serial, "C562-123456789ABC");
    info.provisioned = true;
    ASSERT_TRUE(device_info_valid(&info));

    info.hardware_id = 0u;
    ASSERT_FALSE(device_info_valid(&info));
    info.hardware_id = 0x0050u;

    strcpy(info.serial, "bad serial");
    ASSERT_FALSE(device_info_valid(&info));
    strcpy(info.serial, "SN_0001-A");
    ASSERT_TRUE(device_info_valid(&info));

    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_DEVICE_PAGE0,
                                FLASH_DEVICE_PAGE1,
                                (uint16_t)sizeof(device_info_record_t)), 0);
    ASSERT_EQ(device_info_save(&store, &info), 0);

    device_info_t loaded;
    ASSERT_EQ(device_info_load(&store, &loaded), 0);
    ASSERT_STREQ(loaded.serial, info.serial);
    ASSERT_EQ(loaded.hardware_id, info.hardware_id);
    ASSERT_TRUE(loaded.provisioned);
}

TEST_CASE(device_info_corrupt_record_is_rejected)
{
    device_info_t info;
    device_info_defaults(&info, 0x0050u);
    strcpy(info.serial, "C562-123456789ABC");

    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_DEVICE_PAGE0,
                                FLASH_DEVICE_PAGE1,
                                (uint16_t)sizeof(device_info_record_t)), 0);
    ASSERT_EQ(device_info_save(&store, &info), 0);

    record_info_t record;
    uint8_t buffer[256];
    ASSERT_EQ(record_store_load(&store, buffer, sizeof(buffer), &record), 0);
    uint8_t byte = 0u;
    ASSERT_EQ(fake_flash_read(&g_flash, record.page + RECORD_HEADER_LEN, &byte, 1u), 0);
    byte = (uint8_t)(byte ^ 0x40u);
    ASSERT_EQ(fake_flash_program(&g_flash, record.page + RECORD_HEADER_LEN, &byte, 1u), 0);

    device_info_t loaded;
    ASSERT_EQ(device_info_load(&store, &loaded), -1);
}

/* --- metadata ------------------------------------------------------------ */

TEST_CASE(ota_metadata_roundtrip)
{
    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_META_PAGE0, FLASH_META_PAGE1,
                                (uint16_t)sizeof(ota_metadata_t)), 0);

    ota_metadata_t meta;
    ota_metadata_default(&meta);
    ASSERT_TRUE(ota_metadata_valid(&meta));

    meta.state = OTA_STATE_READY_TO_BOOT;
    meta.image_size = 4096u;
    meta.image_crc32 = 0x12345678u;
    meta.image_version = 0x00010001u;
    ASSERT_EQ(ota_metadata_write(&store, &meta), 0);

    ota_metadata_t loaded;
    ASSERT_EQ(ota_metadata_read(&store, &loaded), 0);
    ASSERT_EQ(loaded.state, (uint32_t)OTA_STATE_READY_TO_BOOT);
    ASSERT_EQ(loaded.image_size, 4096u);
    ASSERT_EQ(loaded.image_crc32, 0x12345678u);

    meta.state = 99u;
    ASSERT_EQ(ota_metadata_write(&store, &meta), -1);
}

TEST_CASE(ota_metadata_corrupt_falls_back_to_defaults)
{
    flash_setup();
    record_flash_if_t iface = make_record_if();
    record_store_t store;
    ASSERT_EQ(record_store_init(&store, &iface, FLASH_META_PAGE0, FLASH_META_PAGE1,
                                (uint16_t)sizeof(ota_metadata_t)), 0);

    ota_metadata_t meta;
    ota_metadata_default(&meta);
    meta.state = OTA_STATE_READY_TO_BOOT;
    meta.image_size = 8192u;
    ASSERT_EQ(ota_metadata_write(&store, &meta), 0);

    record_info_t info;
    uint8_t buffer[512];
    ASSERT_EQ(record_store_load(&store, buffer, sizeof(buffer), &info), 0);

    /* Flip the first payload byte so the record CRC no longer matches. */
    uint8_t byte = 0u;
    ASSERT_EQ(fake_flash_read(&g_flash, info.page + RECORD_HEADER_LEN, &byte, 1u), 0);
    byte = (uint8_t)(byte ^ 0xFFu);
    ASSERT_EQ(fake_flash_program(&g_flash, info.page + RECORD_HEADER_LEN, &byte, 1u), 0);

    ota_metadata_t loaded;
    bool found = true;
    ota_metadata_load_or_default(&store, &loaded, &found);
    ASSERT_FALSE(found);

    ota_metadata_t defaults;
    ota_metadata_default(&defaults);
    ASSERT_EQ(loaded.magic, defaults.magic);
    ASSERT_EQ(loaded.layout_version, defaults.layout_version);
    ASSERT_EQ(loaded.state, defaults.state);
    ASSERT_EQ(loaded.install_attempts, defaults.install_attempts);
}

/* --- image validation ---------------------------------------------------- */

TEST_CASE(image_validate_accepts_good_image)
{
    flash_setup();
    uint8_t image[1024];
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_B_START, image, sizeof(image)), 0);

    image_flash_if_t iface = make_image_if();
    ASSERT_EQ(image_validate(&iface, FLASH_SLOT_B_START, FLASH_SLOT_B_SIZE,
                             sizeof(image), crc, FLASH_SLOT_A_START), IMAGE_OK);
}

TEST_CASE(image_validate_rejects_corrupt_image)
{
    flash_setup();
    uint8_t image[1024];
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_A_START, image, sizeof(image)), 0);

    uint8_t bad = 0x00;
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_A_START + 128u, &bad, 1u), 0);

    image_flash_if_t iface = make_image_if();
    ASSERT_EQ(image_validate(&iface, FLASH_SLOT_A_START, FLASH_SLOT_A_SIZE,
                             sizeof(image), crc, FLASH_SLOT_A_START), IMAGE_ERR_CRC);
}

TEST_CASE(image_validate_rejects_bad_size)
{
    flash_setup();
    image_flash_if_t iface = make_image_if();
    ASSERT_EQ(image_validate(&iface, FLASH_SLOT_A_START, FLASH_SLOT_A_SIZE, 4u, 0u,
                             FLASH_SLOT_A_START), IMAGE_ERR_SIZE);
    ASSERT_EQ(image_validate(&iface, FLASH_SLOT_A_START, FLASH_SLOT_A_SIZE,
                             FLASH_SLOT_A_SIZE + 1u, 0u, FLASH_SLOT_A_START),
              IMAGE_ERR_SIZE);
}

/* --- OTA receiver -------------------------------------------------------- */

static void fill_manifest_sha(ota_begin_payload_t *begin, const uint8_t *image,
                              uint32_t size)
{
    sha256_t sha;
    sha256_init(&sha);
    sha256_update(&sha, image, size);
    sha256_final(&sha, begin->image_sha256);
    /* The receiver only requires a non-zero signature; Ed25519 verification
     * happens in the bootloader tests. */
    memset(begin->signature, 0x5A, sizeof(begin->signature));
}

static void make_manifest(ota_begin_payload_t *begin, const uint8_t *image,
                          uint32_t size, uint32_t crc, uint32_t version)
{
    memset(begin, 0, sizeof(*begin));
    begin->image_size = size;
    begin->image_crc32 = crc;
    begin->version = version;
    begin->build_id = 7u;
    begin->hardware_id = 0x0050u;
    fill_manifest_sha(begin, image, size);
}

static ota_reason_t feed_range(ota_receiver_t *receiver, const uint8_t *image,
                               uint32_t from, uint32_t to, uint32_t step)
{
    ota_reason_t reason = OTA_REASON_NONE;
    for (uint32_t offset = from; offset < to; offset += step) {
        uint32_t n = to - offset;
        if (n > step) {
            n = step;
        }
        reason = ota_receiver_data(receiver, offset, image + offset, n,
                                   offset + 1u);
        if (reason != OTA_REASON_NONE) {
            return reason;
        }
    }
    return reason;
}

/* Re-create the receiver from persisted metadata, as the application does after
 * a reset. The fake flash keeps its contents across this call. */
static int reboot_receiver(receiver_context_t *ctx, ota_receiver_t *receiver)
{
    ota_metadata_t meta;
    if (receiver_setup(ctx, receiver, 0x0050u) != 0) {
        return -1;
    }
    if (ota_metadata_read(&ctx->install.meta_store, &meta) != 0) {
        return -1;
    }
    ota_receiver_attach_metadata(receiver, &meta);
    ota_receiver_restore(receiver, &meta);
    return 0;
}

TEST_CASE(ota_receiver_full_transfer)
{
    flash_setup();

    receiver_context_t ctx;
    ota_receiver_t receiver;
    ASSERT_EQ(receiver_setup(&ctx, &receiver, 0x0050u), 0);

    uint8_t image[4096];
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);

    ota_begin_payload_t begin;
    memset(&begin, 0, sizeof(begin));
    begin.image_size = sizeof(image);
    begin.image_crc32 = crc;
    begin.version = 0x00010001u;
    begin.build_id = 42u;
    begin.hardware_id = 0x0050u;
    begin.reserved = 0u;
    fill_manifest_sha(&begin, image, sizeof(image));

    ASSERT_EQ(ota_receiver_begin(&receiver, &begin, 0u), OTA_REASON_NONE);

    for (uint32_t offset = 0; offset < sizeof(image); offset += 20u) {
        uint32_t n = sizeof(image) - offset;
        if (n > 20u) {
            n = 20u;
        }
        ASSERT_EQ(ota_receiver_data(&receiver, offset, image + offset, n,
                                    offset + 1u), OTA_REASON_NONE);
    }

    ota_end_payload_t end;
    end.image_size = sizeof(image);
    end.image_crc32 = crc;
    ASSERT_EQ(ota_receiver_end(&receiver, &end, 5000u), OTA_REASON_NONE);
    ASSERT_EQ(receiver.state, OTA_STATE_READY_TO_BOOT);

    /* Slot B must contain exactly the image we sent. */
    uint8_t verify[4096];
    ASSERT_EQ(fake_flash_read(&g_flash, FLASH_SLOT_B_START, verify, sizeof(verify)), 0);
    ASSERT_EQ(memcmp(verify, image, sizeof(image)), 0);

    ota_metadata_t meta;
    ASSERT_EQ(ota_metadata_read(&ctx.install.meta_store, &meta), 0);
    ASSERT_EQ(meta.state, (uint32_t)OTA_STATE_READY_TO_BOOT);
    ASSERT_EQ(meta.image_size, (uint32_t)sizeof(image));
    ASSERT_EQ(meta.image_crc32, crc);
    ASSERT_EQ(meta.image_version, 0x00010001u);

    ASSERT_TRUE(ctx.stats.notifications >= 1u);
    ASSERT_EQ(ctx.stats.last_state, OTA_STATE_READY_TO_BOOT);
}

TEST_CASE(ota_receiver_rejects_bad_final_crc)
{
    flash_setup();

    receiver_context_t ctx;
    ota_receiver_t receiver;
    ASSERT_EQ(receiver_setup(&ctx, &receiver, 0x0050u), 0);

    uint8_t image[256];
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);

    ota_begin_payload_t begin;
    begin.image_size = sizeof(image);
    begin.image_crc32 = crc;
    begin.version = 0x00010001u;
    begin.build_id = 1u;
    begin.hardware_id = 0x0050u;
    begin.reserved = 0u;
    ASSERT_EQ(ota_receiver_begin(&receiver, &begin, 0u), OTA_REASON_NONE);

    ASSERT_EQ(ota_receiver_data(&receiver, 0u, image, sizeof(image), 100u), OTA_REASON_NONE);

    ota_end_payload_t end;
    end.image_size = sizeof(image);
    end.image_crc32 = crc ^ 1u; /* wrong CRC */
    ASSERT_EQ(ota_receiver_end(&receiver, &end, 200u), OTA_REASON_BAD_CRC);
    ASSERT_EQ(receiver.state, OTA_STATE_ERROR);
    ASSERT_EQ(ctx.stats.last_reason, OTA_REASON_BAD_CRC);
}

TEST_CASE(ota_receiver_rejects_out_of_order)
{
    flash_setup();

    receiver_context_t ctx;
    ota_receiver_t receiver;
    ASSERT_EQ(receiver_setup(&ctx, &receiver, 0x0050u), 0);

    uint8_t image[256];
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);

    ota_begin_payload_t begin;
    begin.image_size = sizeof(image);
    begin.image_crc32 = crc;
    begin.version = 0x00010001u;
    begin.build_id = 1u;
    begin.hardware_id = 0x0050u;
    begin.reserved = 0u;
    ASSERT_EQ(ota_receiver_begin(&receiver, &begin, 0u), OTA_REASON_NONE);

    ASSERT_EQ(ota_receiver_data(&receiver, 16u, image + 16u, 16u, 10u),
              OTA_REASON_OUT_OF_ORDER);
    ASSERT_EQ(receiver.state, OTA_STATE_ERROR);
}

TEST_CASE(ota_receiver_timeout)
{
    flash_setup();

    receiver_context_t ctx;
    ota_receiver_t receiver;
    ASSERT_EQ(receiver_setup(&ctx, &receiver, 0x0050u), 0);

    ota_begin_payload_t begin;
    begin.image_size = 4096u;
    begin.image_crc32 = 0x11111111u;
    begin.version = 0x00010001u;
    begin.build_id = 1u;
    begin.hardware_id = 0x0050u;
    begin.reserved = 0u;
    ASSERT_EQ(ota_receiver_begin(&receiver, &begin, 1000u), OTA_REASON_NONE);

    ota_receiver_tick(&receiver, 12000u);
    ASSERT_EQ(receiver.state, OTA_STATE_ERROR);
    ASSERT_EQ(receiver.last_reason, OTA_REASON_TIMEOUT);
}

TEST_CASE(ota_receiver_rejects_downgrade)
{
    flash_setup();

    receiver_context_t ctx;
    ota_receiver_t receiver;
    ASSERT_EQ(receiver_setup(&ctx, &receiver, 0x0050u), 0);
    ota_receiver_set_installed_version(&receiver, 0x00020000u);

    ota_begin_payload_t begin;
    begin.image_size = 4096u;
    begin.image_crc32 = 0x11111111u;
    begin.version = 0x00010000u; /* older than installed */
    begin.build_id = 1u;
    begin.hardware_id = 0x0050u;
    begin.reserved = 0u;
    ASSERT_EQ(ota_receiver_begin(&receiver, &begin, 0u), OTA_REASON_BAD_VERSION);
}

TEST_CASE(ota_receiver_rejects_unsigned_begin)
{
    flash_setup();

    receiver_context_t ctx;
    ota_receiver_t receiver;
    ASSERT_EQ(receiver_setup(&ctx, &receiver, 0x0050u), 0);

    uint8_t image[4096];
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);
    ota_begin_payload_t begin;
    make_manifest(&begin, image, sizeof(image), crc, 0x00010001u);
    memset(begin.signature, 0, sizeof(begin.signature));

    ASSERT_EQ(ota_receiver_begin(&receiver, &begin, 0u), OTA_REASON_BAD_SIGNATURE);
    ASSERT_EQ(receiver.state, OTA_STATE_ERROR);
}

TEST_CASE(ota_receiver_resumes_after_reset)
{
    flash_setup();

    receiver_context_t ctx1;
    ota_receiver_t first;
    ASSERT_EQ(receiver_setup(&ctx1, &first, 0x0050u), 0);

    uint8_t image[20480]; /* two full 8 KB chunks plus a 4 KB tail */
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);
    ota_begin_payload_t begin;
    make_manifest(&begin, image, sizeof(image), crc, 0x00010001u);

    ASSERT_EQ(ota_receiver_begin(&first, &begin, 0u), OTA_REASON_NONE);
    ASSERT_EQ(feed_range(&first, image, 0u, 16384u, 1000u), OTA_REASON_NONE);
    ASSERT_EQ(first.chunk_count, 2u);
    ASSERT_EQ(first.received, 16384u);

    /* Simulated power loss: no suspend, only the chunk-boundary record. */
    receiver_context_t ctx2;
    ota_receiver_t resumed;
    ASSERT_EQ(reboot_receiver(&ctx2, &resumed), 0);
    ASSERT_EQ(resumed.chunk_count, 2u);
    ASSERT_EQ(resumed.received, 16384u);

    ASSERT_EQ(ota_receiver_begin(&resumed, &begin, 100u), OTA_REASON_NONE);
    ASSERT_EQ(resumed.received, 16384u);

    ASSERT_EQ(feed_range(&resumed, image, 16384u, sizeof(image), 700u),
              OTA_REASON_NONE);
    ota_end_payload_t end = {.image_size = sizeof(image), .image_crc32 = crc};
    ASSERT_EQ(ota_receiver_end(&resumed, &end, 200u), OTA_REASON_NONE);
    ASSERT_EQ(resumed.state, OTA_STATE_READY_TO_BOOT);

    uint8_t verify[20480];
    ASSERT_EQ(fake_flash_read(&g_flash, FLASH_SLOT_B_START, verify, sizeof(verify)), 0);
    ASSERT_EQ(memcmp(verify, image, sizeof(image)), 0);
}

TEST_CASE(ota_receiver_resume_truncates_corrupt_chunk)
{
    flash_setup();

    receiver_context_t ctx1;
    ota_receiver_t first;
    ASSERT_EQ(receiver_setup(&ctx1, &first, 0x0050u), 0);

    uint8_t image[24576]; /* exactly three 8 KB chunks */
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);
    ota_begin_payload_t begin;
    make_manifest(&begin, image, sizeof(image), crc, 0x00010001u);

    ASSERT_EQ(ota_receiver_begin(&first, &begin, 0u), OTA_REASON_NONE);
    ASSERT_EQ(feed_range(&first, image, 0u, 16384u, 4096u), OTA_REASON_NONE);
    ASSERT_EQ(first.chunk_count, 2u);

    /* Corrupt the first chunk: the resume must drop both persisted chunks. */
    uint8_t bad = 0x00u;
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_B_START + 100u, &bad, 1u), 0);

    receiver_context_t ctx2;
    ota_receiver_t resumed;
    ASSERT_EQ(reboot_receiver(&ctx2, &resumed), 0);
    ASSERT_EQ(ota_receiver_begin(&resumed, &begin, 100u), OTA_REASON_NONE);
    ASSERT_EQ(resumed.received, 0u);
    ASSERT_EQ(resumed.chunk_count, 0u);

    /* A clean retransmission must still succeed. */
    ASSERT_EQ(feed_range(&resumed, image, 0u, sizeof(image), 4096u), OTA_REASON_NONE);
    ota_end_payload_t end = {.image_size = sizeof(image), .image_crc32 = crc};
    ASSERT_EQ(ota_receiver_end(&resumed, &end, 200u), OTA_REASON_NONE);
    ASSERT_EQ(resumed.state, OTA_STATE_READY_TO_BOOT);
}

TEST_CASE(ota_receiver_resume_mid_chunk_trims_to_boundary)
{
    flash_setup();

    receiver_context_t ctx1;
    ota_receiver_t first;
    ASSERT_EQ(receiver_setup(&ctx1, &first, 0x0050u), 0);

    uint8_t image[12288];
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);
    ota_begin_payload_t begin;
    make_manifest(&begin, image, sizeof(image), crc, 0x00010001u);

    ASSERT_EQ(ota_receiver_begin(&first, &begin, 0u), OTA_REASON_NONE);
    ASSERT_EQ(feed_range(&first, image, 0u, 10000u, 500u), OTA_REASON_NONE);

    /* Transport loss mid-chunk: the persisted prefix is 10 000 bytes, but only
     * chunk 0 is complete and verified. */
    ota_receiver_suspend(&first, OTA_REASON_TIMEOUT, 12000u);
    ASSERT_EQ(first.received, 10000u);
    ASSERT_EQ(first.chunk_count, 1u);

    receiver_context_t ctx2;
    ota_receiver_t resumed;
    ASSERT_EQ(reboot_receiver(&ctx2, &resumed), 0);
    ASSERT_EQ(resumed.received, 8192u);

    ASSERT_EQ(ota_receiver_begin(&resumed, &begin, 100u), OTA_REASON_NONE);
    ASSERT_EQ(resumed.received, 8192u);
    ASSERT_EQ(feed_range(&resumed, image, 8192u, sizeof(image), 500u),
              OTA_REASON_NONE);
    ota_end_payload_t end = {.image_size = sizeof(image), .image_crc32 = crc};
    ASSERT_EQ(ota_receiver_end(&resumed, &end, 200u), OTA_REASON_NONE);
    ASSERT_EQ(resumed.state, OTA_STATE_READY_TO_BOOT);
}

TEST_CASE(ota_receiver_resume_rejects_manifest_change)
{
    flash_setup();

    receiver_context_t ctx1;
    ota_receiver_t first;
    ASSERT_EQ(receiver_setup(&ctx1, &first, 0x0050u), 0);

    uint8_t image[16384];
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);
    ota_begin_payload_t begin;
    make_manifest(&begin, image, sizeof(image), crc, 0x00010001u);

    ASSERT_EQ(ota_receiver_begin(&first, &begin, 0u), OTA_REASON_NONE);
    ASSERT_EQ(feed_range(&first, image, 0u, 8192u, 1024u), OTA_REASON_NONE);

    receiver_context_t ctx2;
    ota_receiver_t resumed;
    ASSERT_EQ(reboot_receiver(&ctx2, &resumed), 0);

    /* Same bytes, different version: the partial prefix must be discarded. */
    ota_begin_payload_t changed = begin;
    changed.version = 0x00010002u;
    ASSERT_EQ(ota_receiver_begin(&resumed, &changed, 100u), OTA_REASON_NONE);
    ASSERT_EQ(resumed.received, 0u);
    ASSERT_EQ(resumed.chunk_count, 0u);
}

/* --- bootloader install -------------------------------------------------- */

static void prepare_ready_metadata(install_context_t *ctx, uint32_t image_size,
                                   uint32_t crc, uint32_t version,
                                   uint32_t previous_version)
{
    ota_metadata_t meta;
    ota_metadata_default(&meta);
    meta.state = OTA_STATE_READY_TO_BOOT;
    meta.image_size = image_size;
    meta.image_crc32 = crc;
    meta.image_version = version;
    meta.previous_version = previous_version;
    ASSERT_EQ(boot_metadata_save(ctx, &meta), 0);
}

TEST_CASE(boot_install_copies_valid_slot_b)
{
    flash_setup();

    install_context_t ctx;
    ASSERT_EQ(install_ctx_init(&ctx), 0);

    uint8_t image[2048];
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_B_START, image, sizeof(image)), 0);
    prepare_ready_metadata(&ctx, sizeof(image), crc, 0x00010001u, 0u);

    boot_install_io_t io = make_boot_io(&ctx);
    boot_install_result_t result;
    ASSERT_EQ(boot_install_run(&io, &ctx.meta_store, &result), 0);
    ASSERT_EQ(result.decision, BOOT_DECISION_BOOT_INSTALLED);
    ASSERT_TRUE(result.installed_now);

    uint8_t verify[2048];
    ASSERT_EQ(fake_flash_read(&g_flash, FLASH_SLOT_A_START, verify, sizeof(verify)), 0);
    ASSERT_EQ(memcmp(verify, image, sizeof(image)), 0);

    ota_metadata_t meta;
    ASSERT_EQ(ota_metadata_read(&ctx.meta_store, &meta), 0);
    ASSERT_EQ(meta.state, (uint32_t)OTA_STATE_INSTALLED);
    ASSERT_EQ(meta.install_attempts, 0u);

    /* Next boot boots slot A without re-installing. */
    boot_install_result_t second;
    ASSERT_EQ(boot_install_run(&io, &ctx.meta_store, &second), 0);
    ASSERT_EQ(second.decision, BOOT_DECISION_BOOT_A);
    ASSERT_FALSE(second.installed_now);
}

TEST_CASE(boot_install_rejects_corrupt_slot_b)
{
    flash_setup();

    install_context_t ctx;
    ASSERT_EQ(install_ctx_init(&ctx), 0);

    uint8_t image[1024];
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_B_START, image, sizeof(image)), 0);
    prepare_ready_metadata(&ctx, sizeof(image), crc, 0x00010001u, 0u);

    /* Flip one byte in slot B. */
    uint8_t zero = 0x00u;
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_B_START + 64u, &zero, 1u), 0);

    boot_install_io_t io = make_boot_io(&ctx);
    boot_install_result_t result;
    ASSERT_EQ(boot_install_run(&io, &ctx.meta_store, &result), 0);
    ASSERT_EQ(result.reason, OTA_REASON_BAD_CRC);
    ASSERT_EQ(result.decision, BOOT_DECISION_STAY); /* slot A is empty */

    ota_metadata_t meta;
    ASSERT_EQ(ota_metadata_read(&ctx.meta_store, &meta), 0);
    ASSERT_EQ(meta.state, (uint32_t)OTA_STATE_ERROR);
}

TEST_CASE(boot_install_retries_after_interrupted_copy)
{
    flash_setup();

    install_context_t ctx;
    ASSERT_EQ(install_ctx_init(&ctx), 0);

    uint8_t image[2048];
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_B_START, image, sizeof(image)), 0);
    prepare_ready_metadata(&ctx, sizeof(image), crc, 0x00010001u, 0u);

    /* Allow the INSTALLING metadata write (header + full 528-byte record) and
     * a few 16-byte copy granules, then fail in the middle of the copy. */
    g_flash.fail_program_after = (int64_t)(g_flash.bytes_programmed + 600u);

    boot_install_io_t io = make_boot_io(&ctx);
    boot_install_result_t interrupted;
    ASSERT_EQ(boot_install_run(&io, &ctx.meta_store, &interrupted), 0);
    ASSERT_EQ(interrupted.reason, OTA_REASON_FLASH_ERROR);
    ASSERT_EQ(interrupted.decision, BOOT_DECISION_STAY);

    ota_metadata_t meta;
    ASSERT_EQ(ota_metadata_read(&ctx.meta_store, &meta), 0);
    ASSERT_EQ(meta.state, (uint32_t)OTA_STATE_INSTALLING);
    ASSERT_EQ(meta.install_attempts, 1u);

    /* Power-on retry: slot B is untouched, so the copy must succeed. */
    g_flash.fail_program_after = -1;
    boot_install_result_t retried;
    ASSERT_EQ(boot_install_run(&io, &ctx.meta_store, &retried), 0);
    ASSERT_EQ(retried.decision, BOOT_DECISION_BOOT_INSTALLED);
    ASSERT_TRUE(retried.installed_now);

    uint8_t verify[2048];
    ASSERT_EQ(fake_flash_read(&g_flash, FLASH_SLOT_A_START, verify, sizeof(verify)), 0);
    ASSERT_EQ(memcmp(verify, image, sizeof(image)), 0);
}

TEST_CASE(boot_install_rejects_downgrade)
{
    flash_setup();

    install_context_t ctx;
    ASSERT_EQ(install_ctx_init(&ctx), 0);

    uint8_t image[512];
    uint32_t crc = build_test_image(image, sizeof(image), FLASH_SLOT_A_START);
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_B_START, image, sizeof(image)), 0);
    prepare_ready_metadata(&ctx, sizeof(image), crc, 0x00010000u, 0x00020000u);

    boot_install_io_t io = make_boot_io(&ctx);
    boot_install_result_t result;
    ASSERT_EQ(boot_install_run(&io, &ctx.meta_store, &result), 0);
    ASSERT_EQ(result.reason, OTA_REASON_BAD_VERSION);
    ASSERT_EQ(result.decision, BOOT_DECISION_STAY);
}

TEST_CASE(boot_install_probes_factory_image)
{
    flash_setup();

    install_context_t ctx;
    ASSERT_EQ(install_ctx_init(&ctx), 0);

    uint8_t image[512];
    (void)build_test_image(image, sizeof(image), FLASH_SLOT_A_START);
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_A_START, image, sizeof(image)), 0);

    boot_install_io_t io = make_boot_io(&ctx);
    boot_install_result_t result;
    ASSERT_EQ(boot_install_run(&io, &ctx.meta_store, &result), 0);
    ASSERT_FALSE(result.metadata_found);
    ASSERT_EQ(result.decision, BOOT_DECISION_BOOT_A);
}

void run_ota_logic_tests(void)
{
    RUN_TEST(record_store_roundtrip);
    RUN_TEST(record_store_torn_write_keeps_previous);
    RUN_TEST(record_store_corrupt_newest_falls_back_to_older);
    RUN_TEST(record_store_torn_first_save_leaves_no_record);
    RUN_TEST(record_store_erase_failure_keeps_previous);
    RUN_TEST(record_store_clear_pages);
    RUN_TEST(config_store_roundtrip);
    RUN_TEST(config_store_rejects_empty);
    RUN_TEST(config_store_corrupt_falls_back_to_unprovisioned);
    RUN_TEST(wifi_credentials_validate_lengths_and_chars);
    RUN_TEST(device_info_serial_from_uid);
    RUN_TEST(device_info_validation_and_roundtrip);
    RUN_TEST(device_info_corrupt_record_is_rejected);
    RUN_TEST(ota_metadata_roundtrip);
    RUN_TEST(ota_metadata_corrupt_falls_back_to_defaults);
    RUN_TEST(image_validate_accepts_good_image);
    RUN_TEST(image_validate_rejects_corrupt_image);
    RUN_TEST(image_validate_rejects_bad_size);
    RUN_TEST(ota_receiver_full_transfer);
    RUN_TEST(ota_receiver_rejects_bad_final_crc);
    RUN_TEST(ota_receiver_rejects_out_of_order);
    RUN_TEST(ota_receiver_timeout);
    RUN_TEST(ota_receiver_rejects_downgrade);
    RUN_TEST(ota_receiver_rejects_unsigned_begin);
    RUN_TEST(ota_receiver_resumes_after_reset);
    RUN_TEST(ota_receiver_resume_truncates_corrupt_chunk);
    RUN_TEST(ota_receiver_resume_mid_chunk_trims_to_boundary);
    RUN_TEST(ota_receiver_resume_rejects_manifest_change);
    RUN_TEST(boot_install_copies_valid_slot_b);
    RUN_TEST(boot_install_rejects_corrupt_slot_b);
    RUN_TEST(boot_install_retries_after_interrupted_copy);
    RUN_TEST(boot_install_rejects_downgrade);
    RUN_TEST(boot_install_probes_factory_image);
}
