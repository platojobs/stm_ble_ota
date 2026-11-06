#include "fake_flash.h"
#include "flash_layout.h"

#include <string.h>
#include <stdbool.h>

void fake_flash_init(fake_flash_t *flash, uint8_t *storage, uint32_t size, uint32_t base)
{
    memset(flash, 0, sizeof(*flash));
    flash->base = base;
    flash->size = size;
    flash->storage = storage;
    flash->fail_erase_after = -1;
    flash->fail_program_after = -1;
    memset(storage, 0xFF, size);
}

void fake_flash_reset(fake_flash_t *flash)
{
    flash->erase_count = 0u;
    flash->program_count = 0u;
    flash->bytes_programmed = 0u;
    flash->fail_erase_after = -1;
    flash->fail_program_after = -1;
}

static bool in_range(const fake_flash_t *flash, uint32_t address, uint32_t length)
{
    if (address < flash->base) {
        return false;
    }
    uint32_t offset = address - flash->base;
    return (offset <= flash->size) && (length <= flash->size - offset);
}

int fake_flash_erase(void *context, uint32_t address)
{
    fake_flash_t *flash = (fake_flash_t *)context;
    if (!in_range(flash, address, FLASH_PAGE_SIZE)) {
        return -1;
    }
    if (flash->fail_erase_after >= 0 &&
        (int64_t)flash->erase_count >= flash->fail_erase_after) {
        return -1;
    }
    flash->erase_count++;
    uint32_t offset = address - flash->base;
    memset(flash->storage + offset, 0xFF, FLASH_PAGE_SIZE);
    return 0;
}

int fake_flash_program(void *context, uint32_t address, const void *data, uint32_t length)
{
    fake_flash_t *flash = (fake_flash_t *)context;
    if (!in_range(flash, address, length)) {
        return -1;
    }
    if (flash->fail_program_after >= 0 &&
        (int64_t)(flash->bytes_programmed + length) > flash->fail_program_after) {
        return -1;
    }
    flash->program_count++;
    flash->bytes_programmed += length;
    memcpy(flash->storage + (address - flash->base), data, length);
    return 0;
}

int fake_flash_read(void *context, uint32_t address, void *out, uint32_t length)
{
    fake_flash_t *flash = (fake_flash_t *)context;
    if (!in_range(flash, address, length)) {
        return -1;
    }
    memcpy(out, flash->storage + (address - flash->base), length);
    return 0;
}
