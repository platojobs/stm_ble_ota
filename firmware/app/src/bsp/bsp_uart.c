// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    bsp_uart.c
 * @brief   USART1/USART2 driver for STM32C562RE.
 *
 * USART1 (PA9/PA10, AF7) talks to the ESP32 AT co-processor, USART2
 * (PA2/PA3, AF7) is the ST-LINK VCP used by the PC test tool.
 */

#include "bsp_uart.h"
#include "stm32c562xx.h"

#include <string.h>

#define RING_SIZE 256u

typedef struct {
    USART_TypeDef *reg;
    uint8_t ring[RING_SIZE];
    volatile uint32_t head;   /* written by ISR  */
    volatile uint32_t tail;   /* read by main    */
} uart_ctx_t;

static uart_ctx_t g_uarts[BSP_UART_COUNT] = {
    { .reg = ESP_UART_INSTANCE },
    { .reg = HOST_UART_INSTANCE },
};

static void gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    uint32_t moder = port->MODER;
    moder &= ~(3u << (pin * 2u));
    moder |= (2u << (pin * 2u)); /* alternate function */
    port->MODER = moder;

    port->OTYPER &= ~(1u << pin);
    uint32_t pupdr = port->PUPDR;
    pupdr &= ~(3u << (pin * 2u));
    pupdr |= (1u << (pin * 2u)); /* pull-up keeps an idle RX line defined */
    port->PUPDR = pupdr;

    uint32_t shift = (pin & 7u) * 4u;
    uint32_t index = (pin < 8u) ? 0u : 1u;
    uint32_t afr = port->AFR[index];
    afr &= ~(0xFu << shift);
    afr |= ((af & 0xFu) << shift);
    port->AFR[index] = afr;
}

static void usart_configure(USART_TypeDef *reg, uint32_t baud)
{
    reg->CR1 = 0u;
    reg->CR2 = 0u;
    reg->CR3 = 0u;
    reg->CR1 = USART_CR1_TE | USART_CR1_RE;
    reg->BRR = (SystemCoreClock + (baud / 2u)) / baud;
    reg->CR1 |= USART_CR1_UE | USART_CR1_RXNEIE;
    reg->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
}

void bsp_uart_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->APB1LENR |= RCC_APB1LENR_USART2EN;
    (void)RCC->AHB2ENR;
    (void)RCC->APB2ENR;
    (void)RCC->APB1LENR;

    gpio_af(GPIOA, 9u, 7u);    /* USART1_TX */
    gpio_af(GPIOA, 10u, 7u);   /* USART1_RX */
    gpio_af(GPIOA, 2u, 7u);    /* USART2_TX */
    gpio_af(GPIOA, 3u, 7u);    /* USART2_RX */

    usart_configure(USART1, ESP_UART_BAUD);
    usart_configure(USART2, HOST_UART_BAUD);

    NVIC_SetPriority(USART1_IRQn, 2u);
    NVIC_SetPriority(USART2_IRQn, 2u);
    NVIC_EnableIRQ(USART1_IRQn);
    NVIC_EnableIRQ(USART2_IRQn);
}

static void uart_isr(uart_ctx_t *uart)
{
    uint32_t isr = uart->reg->ISR;

    if ((isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) != 0u) {
        uart->reg->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    }

    if ((isr & USART_ISR_RXNE) != 0u) {
        uint8_t byte = (uint8_t)uart->reg->RDR;
        uint32_t next = (uart->head + 1u) % RING_SIZE;
        if (next != uart->tail) {
            uart->ring[uart->head] = byte;
            uart->head = next;
        }
        /* On a full ring the byte is dropped; protocol layers time out. */
    }
}

void USART1_IRQHandler(void)
{
    uart_isr(&g_uarts[BSP_UART_ESP]);
}

void USART2_IRQHandler(void)
{
    uart_isr(&g_uarts[BSP_UART_HOST]);
}

int bsp_uart_write(bsp_uart_id_t id, const void *data, uint32_t length)
{
    if (id >= BSP_UART_COUNT || data == NULL) {
        return -1;
    }
    uart_ctx_t *uart = &g_uarts[id];
    const uint8_t *bytes = (const uint8_t *)data;

    for (uint32_t i = 0; i < length; ++i) {
        uint32_t guard = 0u;
        while ((uart->reg->ISR & USART_ISR_TXE) == 0u) {
            if (++guard > 100000u) {
                return -1;
            }
        }
        uart->reg->TDR = bytes[i];
    }
    return 0;
}

int bsp_uart_write_str(bsp_uart_id_t id, const char *text)
{
    if (text == NULL) {
        return -1;
    }
    return bsp_uart_write(id, text, (uint32_t)strlen(text));
}

bool bsp_uart_read_byte(bsp_uart_id_t id, uint8_t *out)
{
    if (id >= BSP_UART_COUNT || out == NULL) {
        return false;
    }
    uart_ctx_t *uart = &g_uarts[id];
    if (uart->tail == uart->head) {
        return false;
    }
    *out = uart->ring[uart->tail];
    uart->tail = (uart->tail + 1u) % RING_SIZE;
    return true;
}

uint32_t bsp_uart_available(bsp_uart_id_t id)
{
    if (id >= BSP_UART_COUNT) {
        return 0u;
    }
    uart_ctx_t *uart = &g_uarts[id];
    return (uart->head + RING_SIZE - uart->tail) % RING_SIZE;
}

void bsp_uart_flush(bsp_uart_id_t id)
{
    if (id >= BSP_UART_COUNT) {
        return;
    }
    g_uarts[id].tail = g_uarts[id].head;
}
