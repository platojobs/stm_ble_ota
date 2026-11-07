// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    bsp_gpio.h
 * @brief   Board LED and user button.
 */

#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdbool.h>

void bsp_led_init(void);
void bsp_led_set(bool on);
void bsp_led_toggle(void);

void bsp_button_init(void);
/** True while the user button is pressed (active low). */
bool bsp_button_pressed(void);

#endif /* BSP_GPIO_H */
