# Known issues and unverified items

Last updated: 2026-11-07.

Everything below is a statement about what has **not** been verified. Nothing
in this list has been measured on real STM32C562RE silicon; the repository had
no board available while it was written.

## 1. Hardware validation

| Item | Status |
| --- | --- |
| Bootloader boots and jumps to slot A | **not verified on hardware** |
| Slot B → slot A copy, retry and rollback paths | logic tested on host with a fake flash; **not verified on hardware** |
| Flash page erase/program timing and voltage behaviour | **not verified** |
| Power loss during erase/copy; record-store recovery | simulated on host; **not verified on hardware** |
| Sensor sampling (SHT30 + ADC) | **not verified**; no sensor is populated on a bare NUCLEO-C562RE |
| WiFi provisioning against a real access point | **not verified** |
| BLE GATT profile with a real phone/PC central | **not verified** |
| OTA over BLE | **not verified** |
| OTA over USART2 | protocol path covered by host tests; **not verified on hardware** |

Host-side logic tests (394 assertions) cover CRC, frame parsing/resync, record
store torn-write recovery, image validation, OTA state machine, install/copy
decisions and downgrade rejection. They use an in-memory fake flash and do not
model real flash timing.

## 2. Clock and register-level assumptions

- Only the reset clock is used: HSI 144 MHz / 3 = **48 MHz** system clock.
  The 144 MHz PLL configuration is **not implemented** and **not tested**.
- `bsp_uart.c` computes `BRR = (SystemCoreClock + baud/2) / baud`, assuming the
  USART kernel clock equals `SystemCoreClock` and 16x oversampling. The actual
  APB prescalers after reset are assumed to be 1. **Not measured.**
- `bsp_i2c.c` uses `TIMINGR = 0xB0521414` for 100 kHz at 48 MHz. The value was
  derived analytically; SCL frequency and setup/hold times are **not measured**
  with a scope or logic analyser.
- `bsp_adc.c` assumes VDDA/VREF+ = 3.3 V for the millivolt conversion, uses a
  single conversion per sample, and leaves the ADC clock at its reset
  configuration. Sample time and accuracy are **not measured**.
- Flash: the register sequences follow RM0522 and the DFP header fields
  (`PER`/`PNB`/`BKSEL`/`STRT`, quad-word `PG` programming, `WRPERR`/`PGSERR`/
  `STRBERR`/`INCERR`). The page size (8 KB), bank size (256 KB) and the
  read-while-write split are taken from RM0522 and the device header, but the
  erase/program cycles have **not been executed on silicon**.
- `SystemInit()` in the vendored `system_stm32c5xx.c` sets VTOR from
  `__VECTOR_TABLE`; correct VTOR for the application therefore relies on the
  startup/linker combination shipped here. **Not verified by stepping through
  the jump on hardware.**

## 3. Tooling

- **ST official packages**: STM32CubeC5 2.0.0 (first official release,
  13-March-2026) and STM32C5xx DFP 2.0.0/2.1.0 exist. This project vendors a
  DFP header/startup snapshot and does **not** use the HAL, so upgrading the
  vendor snapshot should be a drop-in replacement; the snapshot's exact
  package revision has not been independently re-checked against the current
  ST release notes.
- **OpenOCD**: an STM32C5 flash driver may not exist in older OpenOCD
  releases. `STM32CubeProgrammer` CLI is the supported path here. OpenOCD
  support for C5 is **untested** and may require a newer OpenOCD build.
- **Toolchain**: the build requires the official Arm GNU Toolchain (with
  newlib). The Homebrew `arm-none-eabi-gcc` formula without newlib cannot link
  the project. Only `arm-none-eabi-gcc 15.3.1` was used to produce the
  reference binaries.

## 4. ESP32 co-processor

- **Stock ESP-AT firmware does not expose the `0xFF00` profile.** A custom
  `gatts_data.csv` and a rebuilt BLE data / `mfg_nvs.bin` partition are
  required, as described in `docs/esp32_at_custom_service.md`. That build
  procedure is **not verified** for the ESP-AT release you will use.
- The firmware assumes service index 1 and characteristic indices 0..4. If the
  custom table places the service elsewhere, update `app_config.h`. The
  indices are **not confirmed against a real `AT+BLEGATTSCHAR?` output**.
- ESP-AT's FFF0-style custom table supports 16-bit UUIDs. That is why this
  project uses `0xFF00`/`0xFF01`..`0xFF05`; 128-bit UUIDs would need ESP-AT
  source changes.
- Notification payloads are limited to 20 bytes by the default MTU. An OTA
  DATA frame of 512 bytes therefore takes 27 `AT+BLEGATTSNTFY` round trips.
  BLE throughput is **not measured**; a full 128 KB image over BLE is likely
  to take minutes. The receiver's timeout is an **idle** timeout (10 s
  without progress), not a total transfer timeout.
- `AT+BLEGATTSNTFY` errors for a disconnected central are ignored by the
  service; notifications are best-effort.
- The AT command set used here (`BLEINIT`, `BLEGATTSSRVCRE`, `BLEGATTSNTFY`,
  `+WRITE` URC) is the ESP32 ESP-AT variant. Other ESP-AT chips/versions may
  differ; check the command reference for your build.

## 5. Protocol and product limitations

- Firmware images are checked with CRC-32 only. There is **no signature,
  encryption or secure boot**. Anyone who can reach the OTA channel can flash
  a compatible image.
- Downgrade protection is a simple version comparison (`version < previous` is
  rejected). An equal version is accepted. Erasing metadata through a debugger
  bypasses the check entirely.
- WiFi credentials are stored in plaintext in the config record. This matches
  typical ESP32 AT provisioning but is not a secret store.
- The USART2 PC link shares the ST-LINK virtual COM port. Whether the
  NUCLEO-C562RE solder bridges route the VCP to PA2/PA3 by default is
  **not verified**; check the board user manual before wiring.
- `tools/ota_host.py` BLE mode requires `bleak` and a working custom ESP-AT
  build. The serial mode has no such dependency beyond `pyserial`.
- The `.ota` package format is a host-side convenience container; the
  bootloader does not read it. Only the raw image bytes are transferred.
- The host tool's `--verify` step reconnects and reads `info`; if the device
  fails to boot after an OTA, this reports a timeout rather than a diagnosis.
- Application image must fit in 128 KB (slot A). There is no compression or
  differential update.
- `PC13` button is initialised but unused; no factory-reset gesture is
  implemented.

## 6. Documentation consistency

Pinout, flash partitions, frame layout, UUIDs and timing constants were cross
checked against the headers and the host tests when this file was written.
If code changes, `docs/protocol.md`, `README.md`, `README.zh-CN.md` and this
file must be updated together.
