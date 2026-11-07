// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    bsp_gpio.c
 * @brief   LED (PA5) and user button (PC13) on NUCLEO-C562RE.
 */

#include "bsp_gpio.h"
#include "app_config.h"

void bsp_led_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    (void)RCC->AHB2ENR;

    uint32_t moder = BOARD_LED_PORT->MODER;
    moder &= ~(3u << (BOARD_LED_PIN * 2u));
    moder |= (1u << (BOARD_LED_PIN * 2u));
    BOARD_LED_PORT->MODER = moder;

    BOARD_LED_PORT->OTYPER &= ~(1u << BOARD_LED_PIN);
    bsp_led_set(false);
}

void bsp_led_set(bool on)
{
    if (on) {
        BOARD_LED_PORT->BSRR = (1u << BOARD_LED_PIN);
    } else {
        BOARD_LED_PORT->BSRR = (1u << (BOARD_LED_PIN + 16u));
    }
}

void bsp_led_toggle(void)
{
    BOARD_LED_PORT->ODR ^= (1u << BOARD_LED_PIN);
}

void bsp_button_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    (void)RCC->AHB2ENR;

    uint32_t moder = BOARD_BUTTON_PORT->MODER;
    moder &= ~(3u << (BOARD_BUTTON_PIN * 2u)); /* input */
    BOARD_BUTTON_PORT->MODER = moder;
    BOARD_BUTTON_PORT->PUPDR &= ~(3u << (BOARD_BUTTON_PIN * 2u));
    BOARD_BUTTON_PORT->PUPDR |= (1u << (BOARD_BUTTON_PIN * 2u)); /* pull-up */
}

bool bsp_button_pressed(void)
{
    return (BOARD_BUTTON_PORT->IDR & (1u << BOARD_BUTTON_PIN)) == 0u;
}
