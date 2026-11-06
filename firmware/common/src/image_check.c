#include "image_check.h"
#include "flash_layout.h"
#include "crc.h"

#include <stddef.h>

#define VECTOR_TABLE_MIN_SIZE 8u /* initial SP + reset handler */

static image_check_result_t check_vectors(const image_flash_if_t *flash_if,
                                          uint32_t storage_base,
                                          uint32_t link_base,
                                          uint32_t image_size)
{
    uint32_t vectors[2];

    if (flash_if == NULL || flash_if->read == NULL) {
        return IMAGE_ERR_FLASH;
    }
    if (flash_if->read(flash_if->context, storage_base, vectors, sizeof(vectors)) != 0) {
        return IMAGE_ERR_FLASH;
    }

    uint32_t sp = vectors[0];
    uint32_t reset = vectors[1];

    if (sp < SRAM_START || sp > SRAM_END || (sp & 0x7u) != 0u) {
        return IMAGE_ERR_VECTOR;
    }

    /* Cortex-M reset vectors must have bit 0 set (Thumb state). */
    if ((reset & 1u) == 0u) {
        return IMAGE_ERR_VECTOR;
    }
    uint32_t reset_addr = reset & ~1u;
    if (reset_addr < link_base + VECTOR_TABLE_MIN_SIZE ||
        reset_addr >= link_base + image_size) {
        return IMAGE_ERR_VECTOR;
    }
    return IMAGE_OK;
}

image_check_result_t image_validate(const image_flash_if_t *flash_if,
                                    uint32_t storage_base,
                                    uint32_t slot_size,
                                    uint32_t image_size,
                                    uint32_t expected_crc,
                                    uint32_t link_base)
{
    if (flash_if == NULL || flash_if->read == NULL) {
        return IMAGE_ERR_FLASH;
    }
    if (image_size < VECTOR_TABLE_MIN_SIZE || image_size > slot_size) {
        return IMAGE_ERR_SIZE;
    }

    image_check_result_t result = check_vectors(flash_if, storage_base, link_base,
                                                image_size);
    if (result != IMAGE_OK) {
        return result;
    }

    uint32_t crc = 0xFFFFFFFFu;
    uint8_t chunk[64];
    uint32_t offset = 0u;

    while (offset < image_size) {
        uint32_t n = image_size - offset;
        if (n > sizeof(chunk)) {
            n = (uint32_t)sizeof(chunk);
        }
        if (flash_if->read(flash_if->context, storage_base + offset, chunk, n) != 0) {
            return IMAGE_ERR_FLASH;
        }
        crc = crc32_ieee(chunk, n, crc);
        offset += n;
    }
    crc ^= 0xFFFFFFFFu;
    return (crc == expected_crc) ? IMAGE_OK : IMAGE_ERR_CRC;
}

image_check_result_t image_probe(const image_flash_if_t *flash_if,
                                 uint32_t base,
                                 uint32_t slot_size)
{
    /* For a probe the reset handler only has to land inside the slot. */
    return check_vectors(flash_if, base, base, slot_size);
}
