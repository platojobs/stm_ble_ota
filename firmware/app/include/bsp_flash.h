// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    bsp_flash.h
 * @brief   Internal flash erase/program for the application.
 *
 * The application runs from slot A (bank 1) and only writes slot B and the
 * metadata/config pages (bank 2). STM32C5 dual-bank read-while-write allows
 * this without relocating code to SRAM.
 */

#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stdint.h>

int bsp_flash_erase_page(uint32_t page_address);

/** Program @p length bytes (multiple of 16, 16-byte aligned) into erased flash. */
int bsp_flash_program(uint32_t address, const void *data, uint32_t length);

int bsp_flash_read(uint32_t address, void *out, uint32_t length);

#endif /* BSP_FLASH_H */
