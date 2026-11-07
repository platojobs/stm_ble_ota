# Changelog

All notable changes to this project are documented here. Dates are the actual
development dates; versions follow semantic versioning.

## [0.3.0] - 2026-11-07

Not validated on hardware; see `KNOWN_ISSUES.md`.

### Added

- **CI**: GitHub Actions runs the host tests and the ARM build with `-Werror`,
  runs `cppcheck` (`warning,performance,portability` over firmware and tests),
  uploads firmware artefacts and, for `v*` tags, builds a signed `.ota` with
  `OTA_SIGNING_KEY_B64` and publishes a GitHub Release with `SHA256SUMS`.
- **Version injection**: CMake derives `FW_VERSION`/`FW_BUILD_ID` from
  `git describe` and `tools/build.sh` wraps configure, build, packaging and
  checksums; `-DFW_VERSION=1.2.3 -DFW_BUILD_ID=42` overrides both.
- **Unified error codes**: 16-bit grouped codes (`0x01xx` arguments, `0x02xx`
  flash, `0x03xx` OTA, `0x04xx` image, `0x05xx` network, `0x06xx` sensors,
  `0x07xx` system) with `err_code_from_ota_reason`, `err_code_from_image_result`,
  `err_code_name` and `err_code_domain_name`; full table in
  `docs/error_codes.md`.
- **Fault log**: 8-entry RAM ring buffer with timestamp, code and detail,
  raised from OTA, WiFi and sensor failures (sensor failures are recorded once
  per episode). Readable as JSON through the CLI `log` command and as
  `fault_count`/`last_err`/`last_err_name` in `info`.
- **Device identity**: serial derived from the 96-bit MCU UID
  (`C562-XXXXXXXXXXXX`), hardware id and provisioning flag persisted in two
  alternating records in bank 2 pages 48/49, so identity survives factory
  reset. CLI `device`; `info` reports `serial` and `provisioned`.
- **Production self-test**: CLI `selftest` checks flash read-back, I2C/SHT30,
  ADC and BLE readiness, returns a bitmask and raises `SELFTEST_FAILED`.

### Changed

- **WiFi credentials at rest**: config record version 2 stores the SSID and
  password as ciphertext XORed with a SHA-256 keystream keyed by
  `SHA-256(MCU UID || "stm-ble-ota/wifi/v2")`. Loading with the wrong key
  fails and clears the output. This is obfuscation, not encryption; see
  `docs/security.md`.
- **Input bounds**: SSID 1..32 and password 0..64 printable ASCII bytes, `"`
  and `\` rejected before an AT command or a flash write; overlong USART2
  lines are discarded whole and reported as `LINE_TOO_LONG`; OTA BEGIN/END
  payload length mismatches are reported as `MALFORMED_FRAME`.
- **Documentation**: `SECURITY.md`, `CONTRIBUTING.md`, `docs/security.md`,
  `docs/versioning.md`, issue and pull-request templates, and a production
  flashing / mass-production section in both READMEs.

### Fixed

- Flash layout documentation now reserves `0x0806_4000` .. `0x0807_FFFF`
  (96 KB) after the device-info pages, matching `flash_layout.h`.

### Notes

- The Ed25519 key pair in the repository is a **reference/example pair** and
  must not be used for any real device. Production requires generating a new
  pair, replacing `ota_public_key.h` and rebuilding the bootloader; this is a
  bootloader update, not an OTA of the application.
- Host tests: 1,581 assertions.

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
