/**
 * @file    boot_flash.c
 * @brief   STM32C5 internal-flash erase/program driver, RAM-resident.
 *
 * Register layout and bit names come from the official CMSIS device header
 * (STM32C5xx DFP 2.1.0). The unlock keys and the 128-bit programming sequence
 * follow RM0522 "Embedded flash memory" (standard STM32 unlock sequence
 * 0x45670123 / 0xCDEF89AB). This code has NOT been exercised on silicon; see
 * KNOWN_ISSUES.md.
 */

#include "boot_flash.h"
#include "stm32c562xx.h"
#include "flash_layout.h"

#include <stddef.h>

#define RAMFUNC __attribute__((section(".ramfunc")))

#define FLASH_UNLOCK_KEY1 0x45670123u
#define FLASH_UNLOCK_KEY2 0xCDEF89ABu

#define FLASH_ERROR_MASK (FLASH_SR_WRPERR | FLASH_SR_PGSERR | \
                          FLASH_SR_STRBERR | FLASH_SR_INCERR)

static RAMFUNC void flash_clear_flags(void)
{
    FLASH->CCR = FLASH_CCR_CLR_EOP | FLASH_CCR_CLR_WRPERR |
                 FLASH_CCR_CLR_PGSERR | FLASH_CCR_CLR_STRBERR |
                 FLASH_CCR_CLR_INCERR;
}

static RAMFUNC int flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
        FLASH->KEYR = FLASH_UNLOCK_KEY1;
        FLASH->KEYR = FLASH_UNLOCK_KEY2;
    }
    return ((FLASH->CR & FLASH_CR_LOCK) != 0u) ? -1 : 0;
}

static RAMFUNC void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static RAMFUNC int flash_wait(void)
{
    while ((FLASH->SR & FLASH_SR_BSY) != 0u) {
        /* Busy: the code performing this wait is in SRAM. */
    }
    return ((FLASH->SR & FLASH_ERROR_MASK) != 0u) ? -1 : 0;
}

static uint32_t bank_of(uint32_t address)
{
    return (address >= (FLASH_BOOT_START + FLASH_BANK_SIZE)) ? 1u : 0u;
}

int RAMFUNC boot_flash_erase_page(uint32_t page_address)
{
    uint32_t bank = bank_of(page_address);
    uint32_t bank_base = FLASH_BOOT_START + bank * FLASH_BANK_SIZE;
    uint32_t page = (page_address - bank_base) / FLASH_PAGE_SIZE;
    int status = -1;
    uint32_t primask;

    if ((page_address & (FLASH_PAGE_SIZE - 1u)) != 0u ||
        page_address < FLASH_BOOT_START ||
        page_address >= (FLASH_BOOT_START + FLASH_TOTAL_SIZE)) {
        return -1;
    }
    if (flash_unlock() != 0) {
        return -1;
    }

    /* No interrupt may fetch from flash while the bank is busy. */
    primask = __get_PRIMASK();
    __disable_irq();

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
    __set_PRIMASK(primask);
    return status;
}

int RAMFUNC boot_flash_program(uint32_t address, const void *data, uint32_t length)
{
    int status = 0;
    uint32_t primask;

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

    primask = __get_PRIMASK();
    __disable_irq();

    flash_clear_flags();
    FLASH->CR |= FLASH_CR_PG;

    for (uint32_t offset = 0; offset < length; offset += 16u) {
        const uint32_t *src = (const uint32_t *)((const uint8_t *)data + offset);
        volatile uint32_t *dst =
            (volatile uint32_t *)(uintptr_t)(address + offset);
        /* One quad-word (128 bits) is written as four 32-bit stores. */
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
    __set_PRIMASK(primask);
    return status;
}

int RAMFUNC boot_flash_read(uint32_t address, void *out, uint32_t length)
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
