/**
 * @file    boot_jump.h
 * @brief   Transfer control from the bootloader to slot A.
 */

#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdint.h>

/**
 * Validate-free jump: set the vector table, stack pointer and reset handler
 * from @p image_base. The caller must have validated the image first.
 */
void boot_jump_to_app(uint32_t image_base) __attribute__((noreturn));

#endif /* BOOT_JUMP_H */
