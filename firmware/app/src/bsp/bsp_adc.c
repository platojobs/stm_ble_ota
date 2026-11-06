/**
 * @file    bsp_adc.c
 * @brief   ADC1 single conversion on channel 0 (PA0).
 *
 * Uses the H5-style ADC register layout present in the STM32C5 device header
 * (ISR/IER/CR/CFGR1/SQR1/DR). VDDA is assumed to be 3.3 V, which is true on
 * the Nucleo board; a custom board must adjust bsp_adc_read_mv().
 */

#include "bsp_adc.h"
#include "app_config.h"
#include "stm32c562xx.h"

#include <stdbool.h>
#include <stddef.h>

#define ADC_TIMEOUT 200000u

static bool wait_flag(volatile uint32_t *reg, uint32_t mask)
{
    for (uint32_t i = 0; i < ADC_TIMEOUT; ++i) {
        if ((*reg & mask) != 0u) {
            return true;
        }
    }
    return false;
}

int bsp_adc_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_ADC12EN;
    (void)RCC->AHB2ENR;

    /* PA0 analog mode. */
    uint32_t moder = GPIOA->MODER;
    moder |= (3u << (0u * 2u));
    GPIOA->MODER = moder;

    ADC1->CR &= ~ADC_CR_ADEN;
    /* Voltage regulator on, then wait for it to settle with a short loop. */
    ADC1->CR |= ADC_CR_ADVREGEN;
    for (volatile uint32_t i = 0; i < 10000u; ++i) {
    }

    /* Calibration requires the ADC to be disabled. */
    ADC1->CR |= ADC_CR_ADCAL;
    if (!wait_flag(&ADC1->CR, ADC_CR_ADCAL)) {
        return -1;
    }

    /* Clear the ready flag, then enable. */
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    if (!wait_flag(&ADC1->ISR, ADC_ISR_ADRDY)) {
        return -1;
    }

    /* One conversion, 12-bit resolution, longest sampling time for a divider. */
    ADC1->CFGR1 = 0u;
    ADC1->SQR1 = (LIGHT_ADC_CHANNEL << ADC_SQR1_SQ1_Pos);
    ADC1->SMPR1 |= ADC_SMPR1_SMP0;
    return 0;
}

int bsp_adc_read_raw(uint16_t *raw)
{
    if (raw == NULL) {
        return -1;
    }

    /* Clear end-of-conversion, start one single conversion. */
    ADC1->ISR = ADC_ISR_EOC;
    ADC1->CR |= ADC_CR_ADSTART;

    if (!wait_flag(&ADC1->ISR, ADC_ISR_EOC)) {
        return -1;
    }
    *raw = (uint16_t)(ADC1->DR & 0x0FFFu);
    return 0;
}

int bsp_adc_read_mv(uint16_t *millivolts)
{
    uint16_t raw;
    if (millivolts == NULL || bsp_adc_read_raw(&raw) != 0) {
        return -1;
    }
    *millivolts = (uint16_t)(((uint32_t)raw * 3300u) / 4095u);
    return 0;
}
