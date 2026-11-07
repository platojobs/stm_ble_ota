# Changelog

All notable changes to this project are documented here. Dates are the actual
development dates; versions follow semantic versioning.

## [0.2.0] - 2026-11-07

Not validated on hardware; see `KNOWN_ISSUES.md`.

### Added

- **Resumable OTA**: progress is persisted at every 8 KB chunk boundary with
  per-chunk CRCs; after a reset the receiver re-verifies the stored prefix,
  truncates to the last valid chunk and `OTA_QUERY` reports a chunk-aligned
  offset. Manifest changes discard stale progress.
- **Signed images**: SHA-256 + Ed25519 (TweetNaCl) in the bootloader, signed
  blob covering digest, size, version, build id and hardware id; the receiver
  rejects all-zero signatures. `tools/firmware_tool.py keygen/pack/inspect`
  handles the keypair and package format 2.
- **Diagnostics CLI** over USART2: version, partition, WiFi, sensor, manual
  OTA session control, factory reset with confirmation, tolerant JSON parsing.
- **Watchdog and rollback**: IWDG started by the bootloader, persisted boot
  attempts, 3-second application confirmation grace period, BACKUP snapshot
  before the first install attempt, automatic restore after three unconfirmed
  boots.
- **Flash layout**: slot A, BACKUP and slot B are 96 KB each; a dedicated
  backup slot replaces the previous in-place-only install path.

### Changed

- `.ota` package format is now 120-byte header (format 2) with SHA-256 and
  signature; `.bin` files are no longer accepted by the packer's OTA flow.
- Version encoding is `0x00MMmmpp`; `APP_VERSION` is `0x00000200` for 0.2.0.
- Host tests: 551 assertions, including resume, signature failure and rollback.

### Notes

- Signing keys are generated locally (`tools/keys/` is git-ignored); private
  keys are never committed. Release builds supply `OTA_SIGNING_KEY` from CI.
- Bootloader 24.6 KB / 32 KB, app 28.8 KB / 96 KB; both fit with margin.

## [0.1.0] - 2026-11-07

Initial reference implementation. Not validated on hardware; see
`KNOWN_ISSUES.md`.

### Added

- **Build system**: CMake presets for the STM32C5 firmware, an
  arm-none-eabi toolchain file that prefers the official Arm GNU Toolchain,
  linker scripts for bootloader and application, and a separate host test
  project.
- **MCU support**: STM32C562RE device header, CMSIS core, CMSIS startup and
  `system_stm32c5xx.c` (reset clock, HSI 144 MHz / 3 = 48 MHz).
- **Flash layout**: 32 KB bootloader, two 128 KB application slots across the
  two 256 KB banks, two-page OTA metadata record store and two-page WiFi
  config record store (8 KB pages).
- **Common layer**: CRC-32/IEEE, image validation (size, vector table, CRC),
  double-page record store with torn-write recovery, WiFi config store, OTA
  metadata, transport-independent frame protocol with resynchronisation, OTA
  receiver state machine, boot install decision logic, EMA/sliding sensor
  filters, ESP-AT URC parser, hex and flat-JSON helpers.
- **Bootloader**: validates slot B, persists `INSTALLING`, copies slot B to
  slot A with RAM-resident erase/program routines, re-validates slot A, retries
  interrupted installs (max 3 attempts), and jumps to the application. LED
  patterns report the decision.
- **Application**:
  - BSP: SysTick millisecond tick, GPIO, interrupt-driven USART1/USART2 with
    ring buffers, I2C1, ADC1, internal-flash driver for slot B.
  - Drivers: SHT30 (single-shot, CRC check) and ESP-AT client (command/URC
    separation, prompt-based BLE notifications).
  - Services: periodic sensor sampling with validity flags, WiFi provisioning
    with persistence and 30 s reconnect, BLE GATT profile handling
    (`0xFF00`/`0xFF01..0xFF05`), OTA reception with progress notifications.
  - Application: USART2 text/binary demultiplexer, JSON command routing,
    info/sensor/OTA notifications over both transports.
- **Host tests**: fake 512 KB flash, fault injection, 394 assertions covering
  CRC, frame parsing, record store power-loss behaviour, image checks, OTA
  transfer errors, install/rollback/retry/downgrade paths.
- **Tools**: `firmware_tool.py` (image inspection and `.ota` packaging),
  `ota_host.py` (serial and BLE provisioning plus full OTA flow), reference
  frame/JSON reassemblers for 20-byte BLE notifications.
- **Documentation**: bilingual README, protocol reference, ESP-AT custom
  service guide, porting checklist, known issues, MIT license.

### Notes

- OTA uses CRC integrity only, with no signature or encryption.
- WiFi credentials are stored in plaintext.
- A custom ESP-AT build is required for the BLE profile.
