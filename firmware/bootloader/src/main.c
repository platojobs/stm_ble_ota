// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    main.c
 * @brief   STM32C562RE bootloader entry point.
 *
 * Boot sequence:
 *   1. configure the status LED,
 *   2. read the OTA metadata (with power-loss-safe fallback),
 *   3. run boot_install_run(): validate slot B, copy it to slot A when a
 *      READY_TO_BOOT image is pending, or keep the existing application,
 *   4. blink the outcome, then jump to slot A when an application is valid.
 *
 * The LED pattern is the only status indicator the bootloader has:
 *   2 short blinks -> existing application is valid
 *   3 short blinks -> a new image was just installed
 *   5 slow blinks  -> no valid application, staying in the bootloader
 */

#include "stm32c562xx.h"
#include "boot_flash.h"
#include "boot_jump.h"
#include "boot_install.h"
#include "record_store.h"
#include "ota_metadata.h"
#include "flash_layout.h"

#include <stdint.h>
#include <stdbool.h>

#define LED_PORT GPIOA
#define LED_PIN  5u

static record_store_t g_meta_store;

static void led_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    (void)RCC->AHB2ENR;

    uint32_t moder = LED_PORT->MODER;
    moder &= ~(3u << (LED_PIN * 2u));
    moder |= (1u << (LED_PIN * 2u)); /* general purpose output */
    LED_PORT->MODER = moder;
    LED_PORT->OTYPER &= ~(1u << LED_PIN); /* push-pull */
    LED_PORT->BSRR = (1u << (LED_PIN + 16u)); /* start with LED off */
}

static void led_on(void)
{
    LED_PORT->BSRR = (1u << LED_PIN);
}

static void led_off(void)
{
    LED_PORT->BSRR = (1u << (LED_PIN + 16u));
}

/* Blocking delay derived from the configured core clock. */
static void delay_ms(uint32_t ms)
{
    SysTick->LOAD = (SystemCoreClock / 1000u) - 1u;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    for (uint32_t i = 0; i < ms; ++i) {
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0u) {
        }
    }
    SysTick->CTRL = 0u;
}

static void blink(uint32_t count, uint32_t on_ms, uint32_t off_ms)
{
    for (uint32_t i = 0; i < count; ++i) {
        led_on();
        delay_ms(on_ms);
        led_off();
        delay_ms(off_ms);
    }
}

static int record_flash_erase(void *context, uint32_t page_address)
{
    (void)context;
    return boot_flash_erase_page(page_address);
}

static int record_flash_program(void *context, uint32_t address,
                                const void *data, uint32_t length)
{
    (void)context;
    return boot_flash_program(address, data, length);
}

static int record_flash_read(void *context, uint32_t address, void *out,
                             uint32_t length)
{
    (void)context;
    return boot_flash_read(address, out, length);
}

static int metadata_save(void *context, const ota_metadata_t *metadata)
{
    (void)context;
    return ota_metadata_write(&g_meta_store, metadata);
}

int main(void)
{
    led_init();

    record_flash_if_t record_if = {
        .context = NULL,
        .erase_page = record_flash_erase,
        .program = record_flash_program,
        .read = record_flash_read,
    };

    if (record_store_init(&g_meta_store, &record_if, FLASH_META_PAGE0,
                          FLASH_META_PAGE1,
                          sizeof(ota_metadata_t)) != 0) {
        blink(5u, 200u, 400u);
        for (;;) {
        }
    }

    boot_install_io_t io = {
        .context = NULL,
        .flash_erase_page = record_flash_erase,
        .flash_program = record_flash_program,
        .flash_read = record_flash_read,
        .metadata_save = metadata_save,
    };

    boot_install_result_t result;
    if (boot_install_run(&io, &g_meta_store, &result) != 0) {
        blink(5u, 200u, 400u);
        for (;;) {
        }
    }

    switch (result.decision) {
    case BOOT_DECISION_BOOT_A:
        blink(2u, 40u, 120u);
        boot_jump_to_app(FLASH_SLOT_A_START);
        break;
    case BOOT_DECISION_BOOT_INSTALLED:
        blink(3u, 40u, 120u);
        boot_jump_to_app(FLASH_SLOT_A_START);
        break;
    case BOOT_DECISION_STAY:
    default:
        blink(5u, 200u, 400u);
        break;
    }

    /* Stay in the bootloader with a periodic heartbeat. */
    for (;;) {
        blink(5u, 200u, 400u);
        delay_ms(1000u);
    }
}
