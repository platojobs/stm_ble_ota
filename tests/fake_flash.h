#ifndef FAKE_FLASH_H
#define FAKE_FLASH_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t base;
    uint32_t size;
    uint8_t *storage;

    uint32_t erase_count;
    uint32_t program_count;
    uint32_t bytes_programmed;

    int64_t  fail_erase_after;    /* -1 disabled; fail the Nth erase */
    int64_t  fail_program_after;  /* -1 disabled; stop after N programmed bytes */
} fake_flash_t;

void fake_flash_init(fake_flash_t *flash, uint8_t *storage, uint32_t size, uint32_t base);
void fake_flash_reset(fake_flash_t *flash);
int fake_flash_erase(void *context, uint32_t address);
int fake_flash_program(void *context, uint32_t address, const void *data, uint32_t length);
int fake_flash_read(void *context, uint32_t address, void *out, uint32_t length);

#endif /* FAKE_FLASH_H */
