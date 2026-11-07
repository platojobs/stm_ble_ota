// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "test_framework.h"
#include "fake_flash.h"
#include "flash_layout.h"
#include "crc.h"
#include "record_store.h"
#include "ota_metadata.h"
#include "ota_receiver.h"
#include "ota_protocol.h"
#include "boot_install.h"
#include "boot_policy.h"
#include "image_check.h"
#include "sha256.h"
#include "cli_parser.h"

#include <string.h>

/* --- shared fake flash --------------------------------------------------- */

static uint8_t g_storage[FLASH_TOTAL_SIZE];
static fake_flash_t g_flash;

static void flash_setup(void)
{
    fake_flash_init(&g_flash, g_storage, FLASH_TOTAL_SIZE, FLASH_BOOT_START);
}

/* --- boot policy (pure decision function) -------------------------------- */

TEST_CASE(boot_policy_boots_when_healthy)
{
    ASSERT_EQ(boot_policy_evaluate(0u, true, true), BOOT_ACTION_BOOT_CURRENT);
    ASSERT_EQ(boot_policy_evaluate(2u, true, true), BOOT_ACTION_BOOT_CURRENT);
}

TEST_CASE(boot_policy_rolls_back_after_three_unconfirmed)
{
    ASSERT_EQ(boot_policy_evaluate(3u, true, true), BOOT_ACTION_ROLLBACK);
    ASSERT_EQ(boot_policy_evaluate(5u, true, true), BOOT_ACTION_ROLLBACK);
}

TEST_CASE(boot_policy_holds_when_no_backup_and_broken_slot)
{
    ASSERT_EQ(boot_policy_evaluate(3u, false, false), BOOT_ACTION_HOLD);
    ASSERT_EQ(boot_policy_evaluate(0u, false, false), BOOT_ACTION_HOLD);
}

TEST_CASE(boot_policy_rolls_back_when_slot_a_is_broken_but_backup_exists)
{
    ASSERT_EQ(boot_policy_evaluate(0u, true, false), BOOT_ACTION_ROLLBACK);
}

/* --- CLI parser ---------------------------------------------------------- */

TEST_CASE(cli_parser_recognises_commands)
{
    ASSERT_EQ(cli_parse("help", 4), CLI_HELP);
    ASSERT_EQ(cli_parse("version", 7), CLI_VERSION);
    ASSERT_EQ(cli_parse("part", 4), CLI_PART);
    ASSERT_EQ(cli_parse("wifi", 4), CLI_WIFI);
    ASSERT_EQ(cli_parse("sensor", 6), CLI_SENSOR);
    ASSERT_EQ(cli_parse("ota start", 9), CLI_OTA_START);
    ASSERT_EQ(cli_parse("ota status", 10), CLI_OTA_STATUS);
    ASSERT_EQ(cli_parse("ota abort", 9), CLI_OTA_ABORT);
    ASSERT_EQ(cli_parse("factory reset", 13), CLI_FACTORY_RESET);
}

TEST_CASE(cli_parser_tolerates_case_and_spaces)
{
    ASSERT_EQ(cli_parse("  VERSION  ", 10), CLI_VERSION);
    ASSERT_EQ(cli_parse("OTA   START", 11), CLI_OTA_START);
    ASSERT_EQ(cli_parse("\tFactory\tReset", 14), CLI_FACTORY_RESET);
    ASSERT_EQ(cli_parse("PARTITION", 9), CLI_PART);
}

TEST_CASE(cli_parser_rejects_unknown_and_too_long)
{
    ASSERT_EQ(cli_parse("reboot", 6), CLI_UNKNOWN);
    ASSERT_EQ(cli_parse("ota fly", 7), CLI_UNKNOWN);
    ASSERT_EQ(cli_parse("", 0), CLI_UNKNOWN);

    char long_line[CLI_MAX_LINE + 2];
    memset(long_line, 'a', sizeof(long_line));
    ASSERT_EQ(cli_parse(long_line, sizeof(long_line)), CLI_TOO_LONG);
}

/* --- image authentication (signature failure path) ----------------------- */

static const uint8_t k_test_sha256[32] = {
    0xE3, 0x77, 0xE3, 0x45, 0x61, 0xA8, 0x18, 0xB9, 0x9F, 0x4C, 0xFE, 0x20,
    0xBB, 0xBE, 0x53, 0xAD, 0x52, 0xB8, 0x32, 0x77, 0x6F, 0x76, 0x10, 0x66,
    0x98, 0xA0, 0x11, 0x56, 0x67, 0x71, 0x05, 0xE8,
};
static const uint8_t k_test_signature[64] = {
    0xDE, 0xAD, 0xA8, 0xA5, 0x2C, 0xF6, 0xA1, 0x48, 0x5D, 0x48, 0x74, 0x6E,
    0x95, 0xA2, 0x8E, 0x92, 0x4B, 0x6D, 0x2A, 0x9D, 0x94, 0x06, 0xE9, 0x90,
    0xC6, 0x67, 0x4B, 0x10, 0x51, 0x22, 0x9A, 0xFA, 0xF9, 0x4C, 0x61, 0xD0,
    0xEC, 0x30, 0x50, 0x78, 0x8C, 0xEC, 0x0D, 0x69, 0x70, 0x33, 0x1E, 0x14,
    0xAC, 0xEA, 0xB1, 0xBC, 0x44, 0x19, 0x21, 0xDE, 0x58, 0x6E, 0x68, 0xDC,
    0x1B, 0xFB, 0xCE, 0x00,
};

/* Recreate the 256-byte test image whose SHA-256/signature are above. */
static void build_signed_test_image(uint8_t *buffer)
{
    for (uint32_t i = 0; i < 256u; ++i) {
        buffer[i] = (uint8_t)(i * 7u + 3u);
    }
    uint32_t sp = SRAM_END - 0x40u;
    uint32_t reset = (FLASH_SLOT_A_START + 0xFCu) | 1u;
    memcpy(buffer, &sp, sizeof(sp));
    memcpy(buffer + 4u, &reset, sizeof(reset));
}

static image_flash_if_t make_image_if(void)
{
    image_flash_if_t iface;
    iface.context = &g_flash;
    iface.read = fake_flash_read;
    return iface;
}

TEST_CASE(image_validate_accepts_valid_signature)
{
    flash_setup();
    uint8_t image[256];
    build_signed_test_image(image);
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_B_START, image,
                                 sizeof(image)), 0);

    ota_signature_blob_t blob;
    ota_signature_blob_build(&blob, k_test_sha256, sizeof(image),
                             0x00020000u, 7u, 0x0050u);
    image_expectation_t expect = {
        .image_size = sizeof(image),
        .expected_crc = 0x21A57323u,
        .link_base = FLASH_SLOT_A_START,
        .sha256 = k_test_sha256,
        .signed_blob = &blob,
        .signature = k_test_signature,
    };
    image_flash_if_t iface = make_image_if();
    ASSERT_EQ(image_validate_full(&iface, FLASH_SLOT_B_START, FLASH_SLOT_B_SIZE,
                                  &expect), IMAGE_OK);
}

TEST_CASE(image_validate_rejects_bad_signature)
{
    flash_setup();
    uint8_t image[256];
    build_signed_test_image(image);
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_B_START, image,
                                 sizeof(image)), 0);

    ota_signature_blob_t blob;
    ota_signature_blob_build(&blob, k_test_sha256, sizeof(image),
                             0x00020000u, 7u, 0x0050u);
    uint8_t bad_sig[64];
    memcpy(bad_sig, k_test_signature, sizeof(bad_sig));
    bad_sig[0] ^= 0x01u;

    image_expectation_t expect = {
        .image_size = sizeof(image),
        .expected_crc = 0x21A57323u,
        .link_base = FLASH_SLOT_A_START,
        .sha256 = k_test_sha256,
        .signed_blob = &blob,
        .signature = bad_sig,
    };
    image_flash_if_t iface = make_image_if();
    ASSERT_EQ(image_validate_full(&iface, FLASH_SLOT_B_START, FLASH_SLOT_B_SIZE,
                                  &expect), IMAGE_ERR_SIGNATURE);
}

TEST_CASE(image_validate_rejects_bad_sha)
{
    flash_setup();
    uint8_t image[256];
    build_signed_test_image(image);
    image[100] ^= 0xFFu; /* corrupt the image so its SHA no longer matches */
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_B_START, image,
                                 sizeof(image)), 0);

    image_expectation_t expect = {
        .image_size = sizeof(image),
        .expected_crc = 0u, /* skip CRC, only SHA matters here */
        .link_base = FLASH_SLOT_A_START,
        .sha256 = k_test_sha256,
        .signed_blob = NULL,
        .signature = NULL,
    };
    image_flash_if_t iface = make_image_if();
    ASSERT_EQ(image_validate_full(&iface, FLASH_SLOT_B_START, FLASH_SLOT_B_SIZE,
                                  &expect), IMAGE_ERR_SHA256);
}

/* --- resume state machine ------------------------------------------------ */

typedef struct {
    fake_flash_t *flash;
    record_flash_if_t iface;
    record_store_t meta_store;
} ctx_t;

static int ctx_erase(void *c, uint32_t a) { return fake_flash_erase(((ctx_t*)c)->flash, a); }
static int ctx_program(void *c, uint32_t a, const void *d, uint32_t l) { return fake_flash_program(((ctx_t*)c)->flash, a, d, l); }
static int ctx_read(void *c, uint32_t a, void *o, uint32_t l) { return fake_flash_read(((ctx_t*)c)->flash, a, o, l); }
static int ctx_save(void *c, const ota_metadata_t *m) { return ota_metadata_write(&((ctx_t*)c)->meta_store, m); }

typedef struct {
    ctx_t install;
    ota_receiver_io_t io;
    uint32_t notifications;
    ota_state_t last_state;
} rctx_t;

static void rnotify(void *c, ota_state_t s, ota_reason_t r, uint32_t rx,
                    uint32_t tx, uint32_t crc)
{
    (void)r; (void)rx; (void)tx; (void)crc;
    rctx_t *ctx = (rctx_t*)c;
    ctx->notifications++;
    ctx->last_state = s;
}

static int rsetup(rctx_t *ctx, ota_receiver_t *receiver)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->install.flash = &g_flash;
    ctx->install.iface.context = &ctx->install;
    ctx->install.iface.erase_page = ctx_erase;
    ctx->install.iface.program = ctx_program;
    ctx->install.iface.read = ctx_read;
    if (record_store_init(&ctx->install.meta_store, &ctx->install.iface,
                           FLASH_META_PAGE0, FLASH_META_PAGE1,
                           (uint16_t)sizeof(ota_metadata_t)) != 0) {
        return -1;
    }
    ctx->io.context = ctx;
    ctx->io.flash_erase_page = ctx_erase;
    ctx->io.flash_program = ctx_program;
    ctx->io.flash_read = ctx_read;
    ctx->io.metadata_save = ctx_save;
    ctx->io.notify = rnotify;
    return ota_receiver_init(receiver, &ctx->io, FLASH_SLOT_B_START,
                             FLASH_SLOT_B_SIZE, 0x0050u);
}

static uint32_t image_crc_of(const uint8_t *image, uint32_t size)
{
    return crc32_ieee_buf(image, size);
}

TEST_CASE(resume_continues_after_suspend)
{
    flash_setup();

    /* Image is 20 KB: crosses two 8 KB chunks. */
    enum { IMG_SIZE = 20u * 1024u };
    static uint8_t image[IMG_SIZE];
    for (uint32_t i = 0; i < IMG_SIZE; ++i) {
        image[i] = (uint8_t)(i * 131u + 17u);
    }
    uint32_t sp = SRAM_END - 0x40u;
    uint32_t reset = (FLASH_SLOT_A_START + 0xFCu) | 1u;
    memcpy(image, &sp, sizeof(sp));
    memcpy(image + 4u, &reset, sizeof(reset));
    uint32_t crc = image_crc_of(image, IMG_SIZE);

    rctx_t ctx;
    ota_receiver_t receiver;
    ASSERT_EQ(rsetup(&ctx, &receiver), 0);

    ota_begin_payload_t begin;
    memset(&begin, 0, sizeof(begin));
    begin.image_size = IMG_SIZE;
    begin.image_crc32 = crc;
    begin.version = 0x00020000u;
    begin.build_id = 1u;
    begin.hardware_id = 0x0050u;
    sha256(image, IMG_SIZE, begin.image_sha256);
    memset(begin.signature, 0xAA, sizeof(begin.signature));

    ASSERT_EQ(ota_receiver_begin(&receiver, &begin, 0u), OTA_REASON_NONE);

    /* Send the first 10 KB (one full chunk + 2 KB), then suspend. */
    for (uint32_t off = 0; off < 10u * 1024u; off += 512u) {
        uint32_t n = (IMG_SIZE - off < 512u) ? IMG_SIZE - off : 512u;
        ASSERT_EQ(ota_receiver_data(&receiver, off, image + off, n, off + 1u),
                  OTA_REASON_NONE);
    }
    ota_receiver_suspend(&receiver, OTA_REASON_TIMEOUT, 20000u);
    ASSERT_EQ(receiver.state, OTA_STATE_ERROR);

    /* Persisted metadata must remember the partial. */
    ota_metadata_t meta;
    ASSERT_EQ(ota_metadata_read(&ctx.install.meta_store, &meta), 0);
    ASSERT_EQ(meta.state, (uint32_t)OTA_STATE_RECEIVING);
    ASSERT_EQ(meta.chunk_count, 1u); /* only the first 8 KB chunk finalised */
    ASSERT_TRUE(meta.received_bytes >= 8u * 1024u);

    /* New receiver simulates a reboot: restore and resume the same manifest. */
    rctx_t ctx2;
    ota_receiver_t receiver2;
    ASSERT_EQ(rsetup(&ctx2, &receiver2), 0);
    ota_receiver_attach_metadata(&receiver2, &meta);
    ota_receiver_restore(&receiver2, &meta);

    ASSERT_EQ(ota_receiver_begin(&receiver2, &begin, 30000u), OTA_REASON_NONE);
    ASSERT_EQ(receiver2.state, OTA_STATE_RECEIVING);
    ASSERT_TRUE(receiver2.received >= 8u * 1024u);

    /* Send the remainder from the resume offset. */
    for (uint32_t off = receiver2.received; off < IMG_SIZE; off += 512u) {
        uint32_t n = (IMG_SIZE - off < 512u) ? IMG_SIZE - off : 512u;
        ASSERT_EQ(ota_receiver_data(&receiver2, off, image + off, n, off + 1u),
                  OTA_REASON_NONE);
    }

    ota_end_payload_t end;
    end.image_size = IMG_SIZE;
    end.image_crc32 = crc;
    ASSERT_EQ(ota_receiver_end(&receiver2, &end, 60000u), OTA_REASON_NONE);
    ASSERT_EQ(receiver2.state, OTA_STATE_READY_TO_BOOT);

    /* Slot B must hold the full image despite the mid-transfer reset. */
    uint8_t verify[IMG_SIZE];
    ASSERT_EQ(fake_flash_read(&g_flash, FLASH_SLOT_B_START, verify, IMG_SIZE), 0);
    ASSERT_EQ(memcmp(verify, image, IMG_SIZE), 0);
}

TEST_CASE(resume_restarts_when_manifest_changes)
{
    flash_setup();

    enum { IMG_SIZE = 8u * 1024u };
    static uint8_t image[IMG_SIZE];
    for (uint32_t i = 0; i < IMG_SIZE; ++i) image[i] = (uint8_t)i;
    uint32_t sp = SRAM_END - 0x40u;
    uint32_t reset = (FLASH_SLOT_A_START + 0xFCu) | 1u;
    memcpy(image, &sp, sizeof(sp));
    memcpy(image + 4u, &reset, sizeof(reset));
    uint32_t crc = image_crc_of(image, IMG_SIZE);

    rctx_t ctx;
    ota_receiver_t receiver;
    ASSERT_EQ(rsetup(&ctx, &receiver), 0);

    ota_begin_payload_t begin;
    memset(&begin, 0, sizeof(begin));
    begin.image_size = IMG_SIZE;
    begin.image_crc32 = crc;
    begin.version = 0x00020000u;
    begin.build_id = 1u;
    begin.hardware_id = 0x0050u;
    sha256(image, IMG_SIZE, begin.image_sha256);
    memset(begin.signature, 0xAA, sizeof(begin.signature));

    ASSERT_EQ(ota_receiver_begin(&receiver, &begin, 0u), OTA_REASON_NONE);
    ASSERT_EQ(ota_receiver_data(&receiver, 0u, image, 4096u, 1u), OTA_REASON_NONE);
    ota_receiver_suspend(&receiver, OTA_REASON_TIMEOUT, 5000u);

    /* Change the manifest (different version): resume must restart from 0. */
    begin.version = 0x00020001u;
    ota_metadata_t meta;
    ASSERT_EQ(ota_metadata_read(&ctx.install.meta_store, &meta), 0);

    rctx_t ctx2;
    ota_receiver_t receiver2;
    ASSERT_EQ(rsetup(&ctx2, &receiver2), 0);
    ota_receiver_attach_metadata(&receiver2, &meta);
    ota_receiver_restore(&receiver2, &meta);
    ASSERT_EQ(ota_receiver_begin(&receiver2, &begin, 10000u), OTA_REASON_NONE);
    ASSERT_EQ(receiver2.received, 0u);
}

/* ------------------------------------------------------------------------- */

/* Bootloader gate: a valid old slot A must be kept when the staged slot B
 * carries a bad Ed25519 signature. */
TEST_CASE(boot_install_rejects_bad_signature_and_keeps_old_firmware)
{
    flash_setup();

    uint8_t image[256];
    build_signed_test_image(image);
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_A_START, image, sizeof(image)), 0);
    ASSERT_EQ(fake_flash_program(&g_flash, FLASH_SLOT_B_START, image, sizeof(image)), 0);

    ctx_t ctx;
    boot_install_io_t io;
    memset(&ctx, 0, sizeof(ctx));
    ctx.flash = &g_flash;
    ctx.iface.context = &ctx;
    ctx.iface.erase_page = ctx_erase;
    ctx.iface.program = ctx_program;
    ctx.iface.read = ctx_read;
    ASSERT_EQ(record_store_init(&ctx.meta_store, &ctx.iface,
                                FLASH_META_PAGE0, FLASH_META_PAGE1,
                                (uint16_t)sizeof(ota_metadata_t)), 0);

    io.context = &ctx;
    io.flash_read = ctx_read;
    io.flash_erase_page = ctx_erase;
    io.flash_program = ctx_program;
    io.metadata_save = ctx_save;

    ota_metadata_t meta;
    ota_metadata_default(&meta);
    meta.state = (uint32_t)OTA_STATE_READY_TO_BOOT;
    meta.image_size = 256u;
    meta.image_crc32 = 0x21A57323u;
    meta.image_version = 0x00020000u;
    meta.build_id = 7u;
    meta.hardware_id = 0x0050u;
    memcpy(meta.image_sha256, k_test_sha256, sizeof(meta.image_sha256));
    memcpy(meta.image_signature, k_test_signature, sizeof(meta.image_signature));
    meta.image_signature[10] ^= 0x01u; /* one-bit tamper */
    meta.running_size = 256u;
    meta.running_crc32 = 0x21A57323u;
    meta.running_version = 0x00010000u;
    ASSERT_EQ(ota_metadata_write(&ctx.meta_store, &meta), 0);

    boot_install_result_t result;
    ASSERT_EQ(boot_install_run(&io, &ctx.meta_store, &result), 0);
    ASSERT_EQ(result.reason, OTA_REASON_BAD_SIGNATURE);
    ASSERT_EQ(result.decision, BOOT_DECISION_BOOT_A);
    ASSERT_EQ(result.install_attempts, 0u); /* rejected before any copy */

    ota_metadata_t after;
    ASSERT_EQ(ota_metadata_read(&ctx.meta_store, &after), 0);
    ASSERT_EQ(after.state, (uint32_t)OTA_STATE_ERROR);
    ASSERT_EQ(after.install_attempts, 0u);
    ASSERT_EQ(after.running_version, 0x00010000u);
}

void run_v2_features_tests(void)
{
    RUN_TEST(boot_policy_boots_when_healthy);
    RUN_TEST(boot_policy_rolls_back_after_three_unconfirmed);
    RUN_TEST(boot_policy_holds_when_no_backup_and_broken_slot);
    RUN_TEST(boot_policy_rolls_back_when_slot_a_is_broken_but_backup_exists);

    RUN_TEST(cli_parser_recognises_commands);
    RUN_TEST(cli_parser_tolerates_case_and_spaces);
    RUN_TEST(cli_parser_rejects_unknown_and_too_long);

    RUN_TEST(image_validate_accepts_valid_signature);
    RUN_TEST(image_validate_rejects_bad_signature);
    RUN_TEST(image_validate_rejects_bad_sha);
    RUN_TEST(boot_install_rejects_bad_signature_and_keeps_old_firmware);

    RUN_TEST(resume_continues_after_suspend);
    RUN_TEST(resume_restarts_when_manifest_changes);
}
