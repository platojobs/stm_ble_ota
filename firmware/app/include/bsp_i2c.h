/**
 * @file    bsp_i2c.h
 * @brief   Blocking I2C master used by the SHT30 driver.
 */

#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stdint.h>

int bsp_i2c_init(void);

/** Write @p length bytes to a 7-bit address. Returns 0 on success. */
int bsp_i2c_write(uint8_t address7, const uint8_t *data, uint32_t length);

/** Read @p length bytes from a 7-bit address. Returns 0 on success. */
int bsp_i2c_read(uint8_t address7, uint8_t *data, uint32_t length);

#endif /* BSP_I2C_H */
