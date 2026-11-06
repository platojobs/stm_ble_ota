/**
 * @file    sht30.c
 * @brief   SHT30 I2C driver with Sensirion CRC-8 validation.
 */

#include "sht30.h"
#include "bsp_i2c.h"
#include "bsp_time.h"
#include "app_config.h"
#include "crc.h"

#include <stdbool.h>

#define SHT30_CMD_SOFT_RESET 0x30A2u
#define SHT30_CMD_MEASURE    0x2400u /* single shot, high repeatability */

static int send_command(uint16_t command)
{
    uint8_t bytes[2] = { (uint8_t)(command >> 8), (uint8_t)(command & 0xFFu) };
    return bsp_i2c_write(SHT30_ADDRESS, bytes, sizeof(bytes));
}

int sht30_init(void)
{
    if (send_command(SHT30_CMD_SOFT_RESET) != 0) {
        return -1;
    }
    /* Datasheet: reset needs at most 1.5 ms. */
    bsp_delay_ms(2u);
    return 0;
}

int sht30_read(int32_t *temperature_centi, int32_t *humidity_centi)
{
    if (temperature_centi == NULL || humidity_centi == NULL) {
        return -1;
    }

    if (send_command(SHT30_CMD_MEASURE) != 0) {
        return -1;
    }

    /* High-repeatability single shot takes ~15 ms. */
    bsp_delay_ms(20u);

    uint8_t data[6];
    if (bsp_i2c_read(SHT30_ADDRESS, data, sizeof(data)) != 0) {
        return -1;
    }

    if (crc8_sensirion(&data[0], 2u) != data[2] ||
        crc8_sensirion(&data[3], 2u) != data[5]) {
        return -1;
    }

    uint16_t raw_temp = ((uint16_t)data[0] << 8) | data[1];
    uint16_t raw_hum = ((uint16_t)data[3] << 8) | data[4];

    /* T = -45 + 175 * raw / 65535, H = 100 * raw / 65535, in centi units. */
    *temperature_centi = -4500 + (int32_t)((17500u * (uint32_t)raw_temp) / 65535u);
    *humidity_centi = (int32_t)((10000u * (uint32_t)raw_hum) / 65535u);
    return 0;
}
