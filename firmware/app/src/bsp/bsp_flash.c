// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    bsp_flash.c
 * @brief   STM32C5 internal-flash driver for the application.
 *
 * Register names come from the official CMSIS device header; the unlock
 * sequence and 128-bit programming follow RM0522. This is the same low-level
 * sequence as the bootloader driver, but it runs from flash because the
 * application only touches bank 2 while executing from bank 1.
 */

#include "bsp_flash.h"
#include "stm32c562xx.h"
#include "flash_layout.h"

#include <stdbool.h>
#include <stddef.h>

#define FLASH_UNLOCK_KEY1 0x45670123u
#define FLASH_UNLOCK_KEY2 0xCDEF89ABu
#define FLASH_ERROR_MASK (FLASH_SR_WRPERR | FLASH_SR_PGSERR | \
                          FLASH_SR_STRBERR | FLASH_SR_INCERR)

static void flash_clear_flags(void)
{
    FLASH->CCR = FLASH_CCR_CLR_EOP | FLASH_CCR_CLR_WRPERR |
                 FLASH_CCR_CLR_PGSERR | FLASH_CCR_CLR_STRBERR |
                 FLASH_CCR_CLR_INCERR;
}

static int flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
        FLASH->KEYR = FLASH_UNLOCK_KEY1;
        FLASH->KEYR = FLASH_UNLOCK_KEY2;
    }
    return ((FLASH->CR & FLASH_CR_LOCK) != 0u) ? -1 : 0;
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static int flash_wait(void)
{
    uint32_t guard = 0u;
    while ((FLASH->SR & FLASH_SR_BSY) != 0u) {
        if (++guard > 1000000u) {
            return -1;
        }
    }
    return ((FLASH->SR & FLASH_ERROR_MASK) != 0u) ? -1 : 0;
}

static uint32_t bank_of(uint32_t address)
{
    return (address >= (FLASH_BOOT_START + FLASH_BANK_SIZE)) ? 1u : 0u;
}

int bsp_flash_erase_page(uint32_t page_address)
{
    if ((page_address & (FLASH_PAGE_SIZE - 1u)) != 0u ||
        page_address < FLASH_BOOT_START ||
        page_address >= (FLASH_BOOT_START + FLASH_TOTAL_SIZE)) {
        return -1;
    }

    uint32_t bank = bank_of(page_address);
    uint32_t bank_base = FLASH_BOOT_START + bank * FLASH_BANK_SIZE;
    uint32_t page = (page_address - bank_base) / FLASH_PAGE_SIZE;
    int status;

    if (flash_unlock() != 0) {
        return -1;
    }
    flash_clear_flags();

    uint32_t cr = FLASH->CR;
    cr &= ~(FLASH_CR_PNB | FLASH_CR_BKSEL | FLASH_CR_EDATASEL |
            FLASH_CR_PER | FLASH_CR_BER | FLASH_CR_MER);
    cr |= FLASH_CR_PER;
    cr |= (page << FLASH_CR_PNB_Pos) & FLASH_CR_PNB;
    if (bank != 0u) {
        cr |= FLASH_CR_BKSEL;
    }
    FLASH->CR = cr;
    FLASH->CR |= FLASH_CR_STRT;

    status = flash_wait();
    FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_STRT);
    flash_clear_flags();
    flash_lock();
    return status;
}

int bsp_flash_program(uint32_t address, const void *data, uint32_t length)
{
    if (data == NULL || (length % 16u) != 0u || (address % 16u) != 0u) {
        return -1;
    }
    if (address < FLASH_BOOT_START ||
        (address + length) > (FLASH_BOOT_START + FLASH_TOTAL_SIZE)) {
        return -1;
    }
    if (flash_unlock() != 0) {
        return -1;
    }

    int status = 0;
    flash_clear_flags();
    FLASH->CR |= FLASH_CR_PG;

    for (uint32_t offset = 0; offset < length; offset += 16u) {
        const uint32_t *src = (const uint32_t *)((const uint8_t *)data + offset);
        volatile uint32_t *dst =
            (volatile uint32_t *)(uintptr_t)(address + offset);
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
        if (flash_wait() != 0) {
            status = -1;
            break;
        }
    }

    FLASH->CR &= ~FLASH_CR_PG;
    flash_clear_flags();
    flash_lock();
    return status;
}

int bsp_flash_read(uint32_t address, void *out, uint32_t length)
{
    if (out == NULL ||
        address < FLASH_BOOT_START ||
        (address + length) > (FLASH_BOOT_START + FLASH_TOTAL_SIZE)) {
        return -1;
    }
    const uint8_t *src = (const uint8_t *)(uintptr_t)address;
    uint8_t *dst = (uint8_t *)out;
    for (uint32_t i = 0; i < length; ++i) {
        dst[i] = src[i];
    }
    return 0;
}
