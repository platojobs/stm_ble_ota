// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    bsp_i2c.c
 * @brief   I2C1 master for STM32C5 (I2C v2 register set: TIMINGR/CR2/ISR).
 *
 * TIMINGR is computed for a 48 MHz I2C kernel clock and a 100 kHz bus:
 *   PRESC = 11 (250 ns tick), SCLL = SCLH = 20 (5.25 us each),
 *   SCLDEL = 5, SDADEL = 2.
 * This assumes the reset-default kernel clock of 48 MHz. If the clock tree is
 * changed, TIMINGR must be regenerated with STM32CubeMX (see KNOWN_ISSUES).
 */

#include "bsp_i2c.h"
#include "stm32c562xx.h"

#include <stdbool.h>
#include <stddef.h>

#define I2C_TIMEOUT_LOOPS 200000u

#define I2C_TIMING_100KHZ_48MHZ 0xB0521414u

/* In the STM32C5 device header I2C_CR2_NBYTES is a plain mask (not a
 * function-like macro), so the length has to be shifted explicitly. */
#define I2C_CR2_NBYTES_VALUE(length) (((uint32_t)(length) & 0xFFu) << I2C_CR2_NBYTES_Pos)

static void gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    uint32_t moder = port->MODER;
    moder &= ~(3u << (pin * 2u));
    moder |= (2u << (pin * 2u));
    port->MODER = moder;

    port->OTYPER |= (1u << pin); /* open drain */
    uint32_t pupdr = port->PUPDR;
    pupdr &= ~(3u << (pin * 2u));
    pupdr |= (1u << (pin * 2u)); /* pull-up (external pull-ups still required) */
    port->PUPDR = pupdr;

    uint32_t shift = (pin & 7u) * 4u;
    uint32_t index = (pin < 8u) ? 0u : 1u;
    uint32_t afr = port->AFR[index];
    afr &= ~(0xFu << shift);
    afr |= ((af & 0xFu) << shift);
    port->AFR[index] = afr;
}

int bsp_i2c_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    RCC->APB1LENR |= RCC_APB1LENR_I2C1EN;
    (void)RCC->AHB2ENR;
    (void)RCC->APB1LENR;

    gpio_af(GPIOB, 6u, 4u); /* I2C1_SCL */
    gpio_af(GPIOB, 7u, 4u); /* I2C1_SDA */

    I2C1->CR1 = 0u;
    I2C1->TIMINGR = I2C_TIMING_100KHZ_48MHZ;
    I2C1->CR1 = I2C_CR1_PE;

    /* Clear any stale flags. */
    I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF |
                I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
    return 0;
}

static bool wait_rxne(void)
{
    for (uint32_t i = 0; i < I2C_TIMEOUT_LOOPS; ++i) {
        if ((I2C1->ISR & I2C_ISR_RXNE) != 0u) {
            return true;
        }
        if ((I2C1->ISR & I2C_ISR_NACKF) != 0u) {
            return false;
        }
    }
    return false;
}

static bool wait_txis(void)
{
    for (uint32_t i = 0; i < I2C_TIMEOUT_LOOPS; ++i) {
        if ((I2C1->ISR & I2C_ISR_TXIS) != 0u) {
            return true;
        }
        if ((I2C1->ISR & I2C_ISR_NACKF) != 0u) {
            return false;
        }
    }
    return false;
}

static bool wait_stop(void)
{
    for (uint32_t i = 0; i < I2C_TIMEOUT_LOOPS; ++i) {
        if ((I2C1->ISR & I2C_ISR_STOPF) != 0u) {
            return true;
        }
    }
    return false;
}

int bsp_i2c_write(uint8_t address7, const uint8_t *data, uint32_t length)
{
    if (data == NULL || length == 0u || length > 255u) {
        return -1;
    }

    I2C1->CR2 = ((uint32_t)address7 << 1) | I2C_CR2_NBYTES_VALUE(length) |
                I2C_CR2_START | I2C_CR2_AUTOEND;

    for (uint32_t i = 0; i < length; ++i) {
        if (!wait_txis()) {
            I2C1->CR2 |= I2C_CR2_STOP;
            return -1;
        }
        I2C1->TXDR = data[i];
    }

    bool ok = wait_stop();
    uint32_t isr = I2C1->ISR;
    I2C1->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
    if (!ok || (isr & I2C_ISR_NACKF) != 0u) {
        return -1;
    }
    return 0;
}

int bsp_i2c_read(uint8_t address7, uint8_t *data, uint32_t length)
{
    if (data == NULL || length == 0u || length > 255u) {
        return -1;
    }

    I2C1->CR2 = ((uint32_t)address7 << 1) | I2C_CR2_RD_WRN |
                I2C_CR2_NBYTES_VALUE(length) | I2C_CR2_START | I2C_CR2_AUTOEND;

    for (uint32_t i = 0; i < length; ++i) {
        if (!wait_rxne()) {
            I2C1->CR2 |= I2C_CR2_STOP;
            return -1;
        }
        data[i] = (uint8_t)I2C1->RXDR;
    }

    bool ok = wait_stop();
    uint32_t isr = I2C1->ISR;
    I2C1->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
    if (!ok || (isr & I2C_ISR_NACKF) != 0u) {
        return -1;
    }
    return 0;
}
