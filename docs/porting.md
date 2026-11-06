# Porting to another STM32

The project is split so that only the `bsp/` register code and a few constants
depend on the STM32C5. Everything else is plain C11 and is exercised by the
host test suite.

## 1. Layer map

| Layer | Files | MCU-dependent? |
| --- | --- | --- |
| `firmware/common` | CRC, frame protocol, record store, metadata, install, sensor filter, AT parser, JSON, hex | no |
| `firmware/app/src/drivers` | SHT30 over an abstract I2C handle, ESP-AT over abstract UART | no |
| `firmware/app/src/services` | sensor, WiFi, BLE, OTA logic | only pin/timing constants via `app_config.h` |
| `firmware/app/src/bsp` | clock tick, GPIO, USART, I2C, ADC, flash | **yes** |
| `firmware/bootloader` | install decision, jump, flash erase/program | **yes** (flash driver) |
| `firmware/vendor` | CMSIS core + device headers + startup + `system_*.c` | **yes** |

## 2. Files to change

| File | What to change |
| --- | --- |
| `firmware/vendor/` | Replace the device header, `startup_*.c` and `system_*.c` with the DFP of the target family. The startup must define `__PROGRAM_START`/`__cmsis_start`, `__INITIAL_SP`, `__STACK_LIMIT`, and the `.copy.table`/`.zero.table` sections, or the linker script below must be adapted to the startup's expectations. |
| `firmware/common/include/flash_layout.h` | Page size, bank size, slot addresses/sizes, metadata and config pages. |
| `firmware/bootloader/ld/bootloader.ld` | ROM base/size = bootloader region, RAM base/size. |
| `firmware/app/ld/app.ld` | ROM base/size = slot A. |
| `firmware/app/include/app_config.h` | Ports/pins/AFs, baud rates, ADC channel and reference voltage, hardware id, version, BLE indices. |
| `firmware/bootloader/src/boot_flash.c` | Flash register sequence (keys, page erase, programming granularity). Keep the functions in `.ramfunc` if the bootloader erases/programs the same bank it executes from. |
| `firmware/app/src/bsp/bsp_flash.c` | Same register sequence for the application's flash (slot B). |
| `firmware/app/src/bsp/bsp_time.c` | SysTick reload and interrupt handler name. |
| `firmware/app/src/bsp/bsp_uart.c` | USART instance, clock enable, BRR calculation, IRQ handler names. |
| `firmware/app/src/bsp/bsp_i2c.c` | I2C instance, timing register value. |
| `firmware/app/src/bsp/bsp_adc.c` | ADC instance, channel, calibration sequence, reference voltage. |
| `firmware/bootloader/src/boot_jump.c` | NVIC/VTOR/MSP sequence (Cortex-M0/M0+ need a different VTOR story: no VTOR register). |
| `firmware/app/src/app/main.c` | Only if GPIO/user LED timing differs; it uses `bsp_*` APIs. |
| `CMakeLists.txt`, `cmake/toolchain-arm-none-eabi.cmake` | `MCU_DEFINES` (`STM32C562xx`), CPU flags (`-mcpu`, `-mfpu`), startup/system filenames in `firmware/app/CMakeLists.txt` and `firmware/bootloader/CMakeLists.txt`. |
| `firmware/vendor/cmsis/Include` | Use the CMSIS core matching the target core (M33 here). |

## 3. Flash geometry requirements

The partition scheme assumes:

- 8 KB erasable pages,
- two independent banks of 256 KB (read-while-write),
- slot A in bank 1 and slot B in bank 2.

On a single-bank part (for example STM32F103 with 1 KB/2 KB pages):

- the bootloader and slot A live in the same bank, so erasing slot A stalls
  instruction fetch: all flash code that runs during erase/program must be in
  RAM. On the C5 this is `boot_flash.c` in `.ramfunc`; keep that approach.
- "bank 2" disappears; place slot B after a smaller slot A and metadata at the
  end of flash, and accept that a power loss during the copy is recovered by
  re-copying from slot B (already the design).
- recalculate `FLASH_BANK_SIZE`, `FLASH_PAGE_SIZE`, `FLASH_SLOT_A_SIZE`,
  `FLASH_META_PAGE0/1`, `FLASH_CONFIG_PAGE0/1` and keep the two pages of each
  record store adjacent and equal in size.
- keep slot A size a multiple of the erase page size; the linker script and
  `flash_layout.h` must agree.

## 4. Startup/linker contract

The vendored STM32C5 startup expects these symbols from the linker script:

```
__VECTOR_TABLE   start of .vectors (also used by SystemInit for VTOR)
__INITIAL_SP     initial stack pointer   = __StackTop
__STACK_LIMIT    stack limit             = __StackLimit
.copy.table      list of {src, dst, size} for .data and .ramfunc
.zero.table      list of {dst, size} for .bss
__end__ / end    heap start
```

If your startup uses the classic `_sidata/_sdata/_edata/_sbss/_ebss` symbols
instead, either keep an equivalent `.copy.table`/`.zero.table` generator or
replace the startup with one that matches your linker script. Do not mix the
two conventions.

The application is linked at `0x08008000`; `SystemInit` copies
`__VECTOR_TABLE[0]` into VTOR, so no application-side relocation is required.
The bootloader still sets VTOR before jumping for platforms whose startup does
not.

## 5. Clock assumptions

The BSP uses the reset clock: HSI 144 MHz divided by 3 = 48 MHz system clock
(`SystemCoreClock` from `system_stm32c5xx.c`). Consequences:

- USART BRR is computed as `(SystemCoreClock + baud/2) / baud`, which assumes
  the USART kernel clock equals `SystemCoreClock` and 16x oversampling.
- The I2C `TIMINGR` value `0xB0521414` was derived for 48 MHz / 100 kHz.
- The ADC sample time uses the ADC clock derived from that setup.

If you raise the clock (PLL to 144 MHz), update all three and re-measure the
baud rate. `KNOWN_ISSUES.md` records that 144 MHz operation is not implemented
or tested here.

## 6. Verification after porting

1. `cmake -S tests -B build-host && cmake --build build-host && ./build-host/unit_tests`
   — protocol/persistence logic must still pass; it uses the fake flash.
2. Build bootloader and application for the new target and check the map file:
   `arm-none-eabi-size` must show bootloader ≤ its region and app ≤ slot A.
3. Confirm the vector table of the produced `.bin` starts with a plausible SP
   and a reset vector inside slot A:
   `python3 tools/firmware_tool.py inspect build/stm_ble_ota_app.bin`.
4. Flash the bootloader, then the application, then run one OTA cycle with
   `tools/ota_host.py --serial ... ota ... --verify`.

Items 3 and 4 above have only been validated on the host for the C5 target;
see `KNOWN_ISSUES.md`.
