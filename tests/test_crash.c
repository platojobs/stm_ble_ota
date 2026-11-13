// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "test_framework.h"
#include "crash_dump.h"
#include "crash_log.h"
#include "fake_flash.h"
#include "flash_layout.h"

#include <string.h>

#define TEST_SLOTS 4u

/* The capture API uses 32-bit MCU addresses. On a 64-bit host it is backed by
 * a fake address window instead of a real pointer. */
#define FAKE_MEM_BASE 0x20000000u

static uint32_t g_mem[64];

typedef struct {
    bool fail;
} mem_ctx_t;

static bool mem_read(void *context, uint32_t address, uint32_t *out)
{
    mem_ctx_t *mem = (mem_ctx_t *)context;
    if (mem->fail || out == NULL || (address & 3u) != 0u) {
        return false;
    }
    if (address < FAKE_MEM_BASE ||
        (address - FAKE_MEM_BASE) > (uint32_t)sizeof(g_mem) - 4u) {
        return false;
    }
    *out = g_mem[(address - FAKE_MEM_BASE) / 4u];
    return true;
}

static void frame_default(uint32_t frame[8])
{
    frame[0] = 0x11111111u;
    frame[1] = 0x22222222u;
    frame[2] = 0x33333333u;
    frame[3] = 0x44444444u;
    frame[4] = 0x55555555u;
    frame[5] = 0xFFFFFFF9u;
    frame[6] = 0x08008123u;
    frame[7] = 0x01000000u;
}

TEST_CASE(crash_capture_decodes_frame_and_mmfar)
{
    mem_ctx_t mem = { .fail = false };
    for (uint32_t i = 0; i < 64u; ++i) {
        g_mem[i] = 0xA0000000u + i;
    }

    uint32_t frame[8];
    frame_default(frame);
    uint32_t frame_address = FAKE_MEM_BASE + 32u;
    crash_sys_regs_t regs = {
        .cfsr = (1u << 7), /* MMARVALID */
        .hfsr = 0x40000000u,
        .mmfar = 0xDEADBEEFu,
        .bfar = 0xCAFEBABEu,
    };

    crash_record_t record;
    crash_capture(&record, CRASH_EXCEPTION_HARD_FAULT, frame, &regs,
                  frame_address, mem_read, &mem);

    ASSERT_TRUE(crash_record_valid(&record));
    ASSERT_EQ(record.exception, CRASH_EXCEPTION_HARD_FAULT);
    ASSERT_EQ(record.r0, 0x11111111u);
    ASSERT_EQ(record.r3, 0x44444444u);
    ASSERT_EQ(record.r12, 0x55555555u);
    ASSERT_EQ(record.lr, 0xFFFFFFF9u);
    ASSERT_EQ(record.pc, 0x08008123u);
    ASSERT_EQ(record.xpsr, 0x01000000u);
    ASSERT_EQ(record.sp, frame_address);
    ASSERT_EQ(record.fault_address, 0xDEADBEEFu);
    ASSERT_EQ(record.cfsr, (1u << 7));
    ASSERT_EQ(record.hfsr, 0x40000000u);
    ASSERT_EQ(record.bfar, 0xCAFEBABEu);

    /* Sample starts right above the 8-word exception frame. */
    ASSERT_EQ(record.stack_pointer, frame_address + 32u);
    ASSERT_EQ(record.stack_words[0], g_mem[16]);
    ASSERT_EQ(record.stack_words[7], g_mem[23]);
}

TEST_CASE(crash_capture_prefers_bfar_then_pc)
{
    mem_ctx_t mem = { .fail = false };
    uint32_t frame[8];
    frame_default(frame);

    crash_record_t record;
    crash_sys_regs_t regs = {
        .cfsr = (1u << 15), /* BFARVALID */
        .hfsr = 0u,
        .mmfar = 0x11112222u,
        .bfar = 0x33334444u,
    };
    crash_capture(&record, CRASH_EXCEPTION_BUSFAULT, frame, &regs,
                  FAKE_MEM_BASE + 64u, mem_read, &mem);
    ASSERT_EQ(record.fault_address, 0x33334444u);
    ASSERT_STREQ(crash_exception_name(CRASH_EXCEPTION_BUSFAULT), "BUSFAULT");

    crash_sys_regs_t no_flags = { .cfsr = 0u, .hfsr = 0u, .mmfar = 0u, .bfar = 0u };
    crash_capture(&record, CRASH_EXCEPTION_USAGEFAULT, frame, &no_flags,
                  FAKE_MEM_BASE + 64u, mem_read, &mem);
    ASSERT_EQ(record.fault_address, 0x08008123u);
    ASSERT_STREQ(crash_exception_name(CRASH_EXCEPTION_USAGEFAULT), "USAGEFAULT");
}

TEST_CASE(crash_capture_tolerates_unreadable_stack)
{
    mem_ctx_t mem = { .fail = true };
    uint32_t frame[8];
    frame_default(frame);

    crash_record_t record;
    crash_sys_regs_t regs = { 0u, 0u, 0u, 0u };
    crash_capture(&record, CRASH_EXCEPTION_MEMMANAGE, frame, &regs,
                  FAKE_MEM_BASE, mem_read, &mem);

    ASSERT_TRUE(crash_record_valid(&record));
    ASSERT_EQ(record.stack_pointer, 0u);
    ASSERT_EQ(record.stack_words[0], 0u);
    ASSERT_EQ(record.fault_address, 0x08008123u);
}

TEST_CASE(crash_record_crc_detects_corruption)
{
    crash_record_t record;
    crash_record_init(&record);
    record.pc = 0x08000100u;
    crash_record_seal(&record);
    ASSERT_TRUE(crash_record_valid(&record));

    record.fault_address ^= 1u;
    ASSERT_FALSE(crash_record_valid(&record));
}

typedef struct {
    fake_flash_t flash;
    uint8_t storage[TEST_SLOTS * FLASH_PAGE_SIZE];
    record_flash_if_t iface;
    crash_log_t log;
} log_fixture_t;

static void fixture_init(log_fixture_t *fixture)
{
    fake_flash_init(&fixture->flash, fixture->storage, (uint32_t)sizeof(fixture->storage),
                    0x08064000u);
    fixture->iface.context = &fixture->flash;
    fixture->iface.erase_page = fake_flash_erase;
    fixture->iface.program = fake_flash_program;
    fixture->iface.read = fake_flash_read;
    (void)crash_log_init(&fixture->log, &fixture->iface, 0x08064000u, TEST_SLOTS);
}

static void make_record(crash_record_t *record, uint32_t pc, uint32_t uptime)
{
    crash_record_init(record);
    record->exception = CRASH_EXCEPTION_HARD_FAULT;
    record->pc = pc;
    record->lr = pc + 1u;
    record->fault_address = pc;
    record->firmware_version = 0x00000400u; /* 0.4.0 */
    record->uptime_ms = uptime;
    crash_record_seal(record);
}

TEST_CASE(crash_log_appends_and_reads_in_age_order)
{
    log_fixture_t fixture;
    fixture_init(&fixture);

    crash_record_t first;
    make_record(&first, 0x08001000u, 100u);
    ASSERT_EQ(crash_log_append(&fixture.log, &first), 0);
    ASSERT_EQ(first.sequence, 1u);

    crash_record_t second;
    make_record(&second, 0x08002000u, 200u);
    ASSERT_EQ(crash_log_append(&fixture.log, &second), 0);
    ASSERT_EQ(second.sequence, 2u);

    ASSERT_EQ(crash_log_count(&fixture.log), 2u);

    crash_record_t out;
    ASSERT_TRUE(crash_log_get(&fixture.log, 0u, &out));
    ASSERT_TRUE(crash_record_valid(&out));
    ASSERT_EQ(out.sequence, 1u);
    ASSERT_EQ(out.pc, 0x08001000u);
    ASSERT_EQ(out.firmware_version, 0x00000400u);

    ASSERT_TRUE(crash_log_get(&fixture.log, 1u, &out));
    ASSERT_EQ(out.sequence, 2u);
    ASSERT_EQ(out.pc, 0x08002000u);

    ASSERT_FALSE(crash_log_get(&fixture.log, 2u, &out));
}

TEST_CASE(crash_log_ring_overwrites_oldest)
{
    log_fixture_t fixture;
    fixture_init(&fixture);

    for (uint32_t i = 1u; i <= TEST_SLOTS + 2u; ++i) {
        crash_record_t record;
        make_record(&record, 0x08000000u + i, i);
        ASSERT_EQ(crash_log_append(&fixture.log, &record), 0);
    }

    ASSERT_EQ(crash_log_count(&fixture.log), TEST_SLOTS);

    crash_record_t out;
    ASSERT_TRUE(crash_log_get(&fixture.log, 0u, &out));
    ASSERT_EQ(out.pc, 0x08000003u); /* the first two records were recycled */
    ASSERT_EQ(out.sequence, 3u);

    ASSERT_TRUE(crash_log_get(&fixture.log, TEST_SLOTS - 1u, &out));
    ASSERT_EQ(out.pc, 0x08000006u);
    ASSERT_EQ(out.sequence, 6u);
}

TEST_CASE(crash_log_skips_corrupt_slots)
{
    log_fixture_t fixture;
    fixture_init(&fixture);

    for (uint32_t i = 1u; i <= 3u; ++i) {
        crash_record_t record;
        make_record(&record, 0x08000000u + i, i);
        ASSERT_EQ(crash_log_append(&fixture.log, &record), 0);
    }
    ASSERT_EQ(crash_log_count(&fixture.log), 3u);

    /* Destroy the oldest slot (slot 0 holds the first record). */
    memset(fixture.storage, 0x00, 16u);
    ASSERT_EQ(crash_log_count(&fixture.log), 2u);

    crash_record_t out;
    ASSERT_TRUE(crash_log_get(&fixture.log, 0u, &out));
    ASSERT_EQ(out.pc, 0x08000002u);

    /* The ring must still accept new records after the damage. */
    crash_record_t next;
    make_record(&next, 0x08000009u, 9u);
    ASSERT_EQ(crash_log_append(&fixture.log, &next), 0);
    ASSERT_EQ(crash_log_count(&fixture.log), 3u);
    ASSERT_TRUE(crash_log_get(&fixture.log, 2u, &out));
    ASSERT_EQ(out.pc, 0x08000009u);
}

TEST_CASE(crash_log_clear_removes_records)
{
    log_fixture_t fixture;
    fixture_init(&fixture);

    crash_record_t record;
    make_record(&record, 0x08003000u, 1u);
    ASSERT_EQ(crash_log_append(&fixture.log, &record), 0);
    ASSERT_EQ(crash_log_count(&fixture.log), 1u);

    ASSERT_EQ(crash_log_clear(&fixture.log), 0);
    ASSERT_EQ(crash_log_count(&fixture.log), 0u);

    crash_record_t out;
    ASSERT_FALSE(crash_log_get(&fixture.log, 0u, &out));
}

TEST_CASE(crash_log_append_fails_when_erase_fails)
{
    log_fixture_t fixture;
    fixture_init(&fixture);
    fixture.flash.fail_erase_after = 0;

    crash_record_t record;
    make_record(&record, 0x08004000u, 1u);
    ASSERT_EQ(crash_log_append(&fixture.log, &record), -1);
    ASSERT_EQ(crash_log_count(&fixture.log), 0u);
}

TEST_CASE(crash_log_json_reports_records)
{
    log_fixture_t fixture;
    fixture_init(&fixture);

    crash_record_t record;
    make_record(&record, 0x08005000u, 1234u);
    ASSERT_EQ(crash_log_append(&fixture.log, &record), 0);

    char json[2048];
    uint32_t length = crash_log_to_json(&fixture.log, json, sizeof(json));
    ASSERT_TRUE(length > 0u);
    ASSERT_EQ((uint32_t)strlen(json), length);
    ASSERT_TRUE(strstr(json, "\"type\":\"crash\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"count\":1") != NULL);
    ASSERT_TRUE(strstr(json, "\"name\":\"HARDFAULT\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"pc\":134238208") != NULL); /* 0x08005000 */
    ASSERT_TRUE(strstr(json, "\"stack\":[") != NULL);
    ASSERT_TRUE(strstr(json, "\"fw\":\"0.4.0\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"uptime_ms\":1234") != NULL);

    char tiny[8];
    ASSERT_EQ(crash_log_to_json(&fixture.log, tiny, sizeof(tiny)), 0u);
    ASSERT_EQ(crash_log_to_json(NULL, json, sizeof(json)), 0u);
}

void run_crash_tests(void)
{
    RUN_TEST(crash_capture_decodes_frame_and_mmfar);
    RUN_TEST(crash_capture_prefers_bfar_then_pc);
    RUN_TEST(crash_capture_tolerates_unreadable_stack);
    RUN_TEST(crash_record_crc_detects_corruption);
    RUN_TEST(crash_log_appends_and_reads_in_age_order);
    RUN_TEST(crash_log_ring_overwrites_oldest);
    RUN_TEST(crash_log_skips_corrupt_slots);
    RUN_TEST(crash_log_clear_removes_records);
    RUN_TEST(crash_log_append_fails_when_erase_fails);
    RUN_TEST(crash_log_json_reports_records);
}
