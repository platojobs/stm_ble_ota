/**
 * @file    bsp_adc.h
 * @brief   Single-channel ADC used by the light/voltage sensor.
 */

#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdint.h>

int bsp_adc_init(void);

/** Perform one 12-bit conversion on ADC1_IN0 (PA0). Returns 0 on success. */
int bsp_adc_read_raw(uint16_t *raw);

/** Convenience wrapper: returns millivolts assuming VDDA = 3300 mV. */
int bsp_adc_read_mv(uint16_t *millivolts);

#endif /* BSP_ADC_H */
