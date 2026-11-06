/**
 * @file    sht30.h
 * @brief   SHT30 temperature/humidity sensor driver (I2C).
 */

#ifndef SHT30_H
#define SHT30_H

#include <stdint.h>

/** Issue a soft reset. Returns 0 on success. */
int sht30_init(void);

/**
 * Trigger a single high-repeatability measurement and wait for it.
 * Values are returned in hundredths: 23.45 degC -> 2345, 41.2 %RH -> 4120.
 * Returns 0 only when both CRC checks pass.
 */
int sht30_read(int32_t *temperature_centi, int32_t *humidity_centi);

#endif /* SHT30_H */
