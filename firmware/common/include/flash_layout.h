// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    flash_layout.h
 * @brief   Flash partition map for STM32C562RE (512 KB dual-bank, 8 KB pages).
 *
 * All values below are derived from the STM32C5 memory architecture:
 *   - program flash is 512 KB, split into two 256 KB banks,
 *   - each bank contains 32 pages of 8 KB,
 *   - page erase is the minimum erase granularity (RM0522, chapter "Embedded
 *     flash memory").
 *
 * Layout rationale
 * ----------------
 * Slot A lives entirely in bank 1, slot B entirely in bank 2. The application
 * executes from slot A while it erases/programs slot B, so the download path
 * uses the dual-bank read-while-write capability and is not stalled by flash
 * operations. The bootloader runs from bank 1 and erases/programs slot A (and
 * the backup slot) in the same bank, so its erase/program routines execute
 * from SRAM (see the bootloader linker script, .ramfunc).
 *
 * Slot A, the rollback backup and slot B are each 96 KB. The application links
 * at slot A and must fit in 96 KB; the same image is downloaded to slot B and
 * copied to slot A by the bootloader. Before replacing slot A the bootloader
 * snapshots it into BACKUP so a failed confirmation can be rolled back. This
 * costs 96 KB of the 512 KB part but keeps the "old firmware stays runnable"
 * guarantee without external memory.
 *
 * Metadata and configuration each occupy two 8 KB pages, used as two
 * alternating records (one record per page) so a power loss can never destroy
 * the last known-good record.
 */

#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

/* The CMSIS device header also defines FLASH_PAGE_SIZE / FLASH_BANK_SIZE.
 * Guard the definitions so both can be included in any order; the values are
 * identical and are the ones documented for STM32C562RE. */
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE             0x2000u   /* 8 KB, STM32C5 program flash page  */
#endif
#ifndef FLASH_BANK_SIZE
#define FLASH_BANK_SIZE             0x40000u  /* 256 KB per bank                   */
#endif
#define FLASH_TOTAL_SIZE            0x80000u  /* 512 KB total program flash        */

/* --- Bank 1: 0x0800_0000 .. 0x0803_FFFF ------------------------------------ */
#define FLASH_BOOT_START            0x08000000u
#define FLASH_BOOT_SIZE             0x8000u    /*  32 KB, pages 0..3                */

#define FLASH_SLOT_A_START          0x08008000u
#define FLASH_SLOT_A_SIZE           0x18000u   /*  96 KB, pages 4..15               */

#define FLASH_BACKUP_START          0x08020000u
#define FLASH_BACKUP_SIZE           0x18000u   /*  96 KB, pages 16..27              */

#define FLASH_BANK1_RESERVED_START  0x08038000u
#define FLASH_BANK1_RESERVED_SIZE   0x8000u    /*  32 KB, pages 28..31              */

/* --- Bank 2: 0x0804_0000 .. 0x0807_FFFF ------------------------------------ */
#define FLASH_SLOT_B_START          0x08040000u
#define FLASH_SLOT_B_SIZE           0x18000u   /*  96 KB, pages 32..43              */

#define FLASH_META_PAGE0            0x08058000u /* page 44                          */
#define FLASH_META_PAGE1            0x0805A000u /* page 45                          */
#define FLASH_META_STORE_SIZE       (2u * FLASH_PAGE_SIZE)

#define FLASH_CONFIG_PAGE0          0x0805C000u /* page 46                          */
#define FLASH_CONFIG_PAGE1          0x0805E000u /* page 47                          */
#define FLASH_CONFIG_STORE_SIZE     (2u * FLASH_PAGE_SIZE)

#define FLASH_BANK2_RESERVED_START  0x08060000u
#define FLASH_BANK2_RESERVED_SIZE   0x20000u   /* 128 KB, pages 48..63              */

/* Maximum application image accepted by the OTA downloader. */
#define FLASH_IMAGE_MAX_SIZE        FLASH_SLOT_A_SIZE

/* Resume granularity: one 8 KB page. Persisting only at page boundaries keeps
 * a resumed write safe on ECC flash (the page being resumed is always erased
 * before its first byte is programmed). */
#define OTA_RESUME_CHUNK_SIZE       FLASH_PAGE_SIZE
#define OTA_RESUME_MAX_CHUNKS       (FLASH_SLOT_A_SIZE / OTA_RESUME_CHUNK_SIZE)

/* RAM (STM32C562RE): 128 KB contiguous, 0x2000_0000 .. 0x2001_FFFF. */
#define SRAM_START                  0x20000000u
#define SRAM_SIZE                   0x20000u
#define SRAM_END                    (SRAM_START + SRAM_SIZE)

/* Compile-time guards: the map above is a single contiguous, non-overlapping
 * sequence and the current checks must keep it that way. */
#if (FLASH_BOOT_START + FLASH_BOOT_SIZE) != FLASH_SLOT_A_START
#error "Bootloader and slot A must be contiguous"
#endif
#if (FLASH_SLOT_A_START + FLASH_SLOT_A_SIZE) != FLASH_BACKUP_START
#error "Slot A and backup slot must be contiguous"
#endif
#if (FLASH_BACKUP_START + FLASH_BACKUP_SIZE) != FLASH_BANK1_RESERVED_START
#error "Backup slot layout mismatch"
#endif
#if (FLASH_BANK1_RESERVED_START + FLASH_BANK1_RESERVED_SIZE) != \
    (FLASH_BOOT_START + 0x40000u)
#error "Bank 1 reserved area must end at the bank boundary"
#endif
#if (FLASH_SLOT_B_START + FLASH_SLOT_B_SIZE) != FLASH_META_PAGE0
#error "Slot B layout mismatch"
#endif
#if (FLASH_META_PAGE1 + FLASH_PAGE_SIZE) != FLASH_CONFIG_PAGE0
#error "Metadata and config stores must be adjacent"
#endif
#if (FLASH_CONFIG_PAGE1 + FLASH_PAGE_SIZE) != FLASH_BANK2_RESERVED_START
#error "Config store layout mismatch"
#endif
#if (FLASH_BANK2_RESERVED_START + FLASH_BANK2_RESERVED_SIZE) != \
    (FLASH_BOOT_START + FLASH_TOTAL_SIZE)
#error "Bank 2 reserved area must end at the flash boundary"
#endif

#endif /* FLASH_LAYOUT_H */
