// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    boot_flash.h
 * @brief   Minimal internal-flash driver used by the bootloader.
 *
 * Every function is placed in .ramfunc (see bootloader.ld). The bootloader
 * executes from bank 1 and erases/programs slot A, which is also bank 1, so
 * the waiting loop must not fetch instructions from the bank being operated
 * on. Slot B and the metadata pages are in bank 2 and could be written from
 * flash code, but the same driver is used for both for simplicity.
 */

#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdint.h>

/** Erase one 8 KB page. The address must be page aligned. Returns 0 on success. */
int boot_flash_erase_page(uint32_t page_address);

/**
 * Program @p length bytes (multiple of 16, 16-byte aligned) into erased flash.
 * Returns 0 on success.
 */
int boot_flash_program(uint32_t address, const void *data, uint32_t length);

/** Plain read; returns 0 on success. Used by record_store. */
int boot_flash_read(uint32_t address, void *out, uint32_t length);

#endif /* BOOT_FLASH_H */
