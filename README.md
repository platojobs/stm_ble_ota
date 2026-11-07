# stm_ble_ota

English | [简体中文](README.zh-CN.md)

A small, buildable reference for STM32C562RE firmware with:

- periodic sensor sampling (SHT30 over I2C + an ADC light channel),
- WiFi provisioning through an ESP32 running ESP-AT,
- a BLE GATT profile exposed by the same ESP32,
- fail-safe OTA with a bootloader, two application slots and persisted
  upgrade state.

The code is layered (`bsp` / `drivers` / `services` / `app`) and uses
register-level drivers instead of the HAL, so the peripheral surface is small
and easy to port.

**Status:** the firmware and host tests build and pass locally. No part of it
has run on real silicon. See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for the
explicit list of unverified items — do not treat this as a validated product.

## Architecture

```
        ┌──────────────────────────┐
        │  ESP32 (ESP-AT firmware) │
        │  WiFi STA + BLE GATT srv │
        └───────▲──────────▲───────┘
      USART1    │ AT / URC │
                │          │
        ┌───────┴──────────┴───────┐        ┌───────────────┐
        │      STM32C562RE         │  I2C1  │  SHT30 (0x44) │
        │ bootloader + application │◄──────►│               │
        │                          │  ADC1  │  light divider│
        └───────▲──────────────────┘        └───────────────┘
                │ USART2 (JSON + OTA frames)
        ┌───────┴──────────┐
        │ PC / ST-LINK VCP │  tools/ota_host.py
        └──────────────────┘
```

The USART2 link exists so the full provisioning and OTA flow can be exercised
without a BLE central or a working custom ESP-AT build.

## Hardware

| Ref | Part | Notes |
| --- | --- | --- |
| MCU | STM32C562RET6, Cortex-M33 + FPU, 144 MHz max, 512 KB dual-bank flash, 128 KB SRAM | NUCLEO-C562RE board |
| Co-processor | ESP32 module running ESP-AT | WiFi STA + BLE GATT server, connected over UART |
| Sensor | SHT30 | I2C, address `0x44`, 100 kHz |
| Light input | photoresistor / divider | ADC1_IN0, 0..3.3 V |
| Debug/flash | ST-LINK on the Nucleo | SWD + virtual COM port |

The firmware runs from the reset clock: HSI 144 MHz divided by 3 = **48 MHz**.
A 144 MHz PLL setup is not implemented.

### Wiring

| Signal | STM32 pin | Direction | Peer |
| --- | --- | --- | --- |
| ESP TX / RX | PA9 (USART1_TX, AF7) | out | ESP RX |
| ESP RX / TX | PA10 (USART1_RX, AF7) | in | ESP TX |
| Host TX | PA2 (USART2_TX, AF7) | out | ST-LINK VCP RX |
| Host RX | PA3 (USART2_RX, AF7) | in | ST-LINK VCP TX |
| I2C1 SCL | PB6 (AF4) | out | SHT30 SCL, 4.7 kΩ pull-up to 3.3 V |
| I2C1 SDA | PB7 (AF4) | bidir | SHT30 SDA, 4.7 kΩ pull-up to 3.3 V |
| SHT30 VDD/GND | 3.3 V / GND | — | decouple 100 nF |
| Light input | PA0 (ADC1_IN0) | in | divider output, ≤ 3.3 V |
| LED | PA5 | out | Nucleo LD2 |
| Button | PC13 | in | Nucleo B1 (initialised, unused) |

ESP32 and STM32 must share ground. ESP32 logic is 3.3 V; do not feed 5 V into
STM32 pins. USART2 uses 115200 8N1; on the Nucleo, confirm the ST-LINK VCP
solder bridges actually route to PA2/PA3 (see KNOWN_ISSUES).

## Flash layout

Device: 512 KB program flash, two 256 KB banks, 8 KB erase pages.

| Region | Address | Size | Pages | Content |
| --- | --- | --- | --- | --- |
| Bootloader | `0x0800_0000` | 32 KB | 0–3 | bootloader, copy loop in RAM |
| Slot A (application) | `0x0800_8000` | 128 KB | 4–19 | running app, linked here |
| Bank 1 reserved | `0x0802_8000` | 96 KB | 20–31 | unused |
| Slot B (OTA staging) | `0x0804_0000` | 128 KB | 32–47 | downloaded image |
| OTA metadata | `0x0806_0000` | 16 KB | 48–49 | two alternating records |
| WiFi config | `0x0806_4000` | 16 KB | 50–51 | two alternating records |
| Bank 2 reserved | `0x0806_8000` | 96 KB | 52–63 | unused |

Boot flow:

1. Bootloader reads the OTA record store.
2. If state is `READY_TO_BOOT` or `INSTALLING`, validate slot B (size,
   vector table, CRC), then erase slot A and copy B → A. Interrupted copies are
   retried on the next reset (slot B is untouched until the copy starts, and
   only slot A is erased).
3. If slot A validates, jump to it; otherwise stay in the bootloader
   (LED slow-blink loop).
4. `INSTALLED` blinks 3 times before boot, `INSTALLING`/`READY_TO_BOOT` blinks
   2 times, staying in the bootloader blinks slowly.

The copy loop and flash erase/program routines live in `.ramfunc`, because the
bootloader erases the same bank it executes from. The application writes slot B
in the other bank, so read-while-write makes a RAM copy unnecessary there.

## BLE GATT profile

The ESP32 runs ESP-AT as a GATT server. The firmware assumes service index 1
and characteristics 0–4; see [docs/protocol.md](docs/protocol.md) and
[docs/esp32_at_custom_service.md](docs/esp32_at_custom_service.md).

| Char | UUID (16-bit) | Access | Content |
| --- | --- | --- | --- |
| — | `0xFF00` service | — | custom profile |
| 0 | `0xFF01` info | read, notify | JSON device info |
| 1 | `0xFF02` sensor | notify | 20-byte sensor sample |
| 2 | `0xFF03` provisioning | write, notify | `wifi.set` / `wifi.clear` / `wifi.status` |
| 3 | `0xFF04` OTA control | write, notify | BEGIN / END / ABORT, progress, result |
| 4 | `0xFF05` OTA data | write | 512-byte image chunks |

Notifications are limited to 20 bytes by the default MTU; the receiver
reassembles frames across notifications.

## AT interaction

```
AT                 → OK          (up to 5 attempts)
ATE0               → OK
AT+CWMODE=1        → OK
AT+CWAUTOCONN=1    → OK
AT+CWJAP="ssid","pass"
                   ← WIFI CONNECTED
                   ← WIFI GOT IP
                   → OK                 (20 s timeout)
```

BLE setup:

```
AT+BLEINIT=2
AT+BLENAME="STM-BLE-OTA"
AT+BLEGATTSSRVCRE
AT+BLEGATTSSRVSTART
AT+BLEADVPARAM=50,150,0,0,7,0,0,0
AT+BLEADVDATA="020106030300FF"
AT+BLEADVSTART
```

After provisioning, credentials are persisted in the config record store and
re-join is retried every 30 s while `WIFI GOT IP` has not been observed.
`wifi.clear` issues `AT+CWQAP` and erases both config pages.

Full sequences, URC formats and error handling are in
[docs/protocol.md](docs/protocol.md).

## OTA packet format

Transport-independent, 16-byte header + payload (≤ 512 bytes):

```
0  u8  sync0  = 0xA5
1  u8  sync1  = 0x5A
2  u8  type       0x10 BEGIN, 0x11 DATA, 0x12 END, 0x13 ABORT,
                  0x14 PROGRESS, 0x15 RESULT
3  u8  flags      must be 0
4  u16 seq
6  u16 payload_len
8  u32 offset     byte offset, DATA only
12 u32 payload_crc32   CRC-32/IEEE of payload
16 N  payload
```

BEGIN payload: `image_size`, `image_crc32`, `version` (`0x00MMmmpp`),
`build_id`, `hardware_id` (must equal `0x0050`), reserved.

States: `IDLE → RECEIVING → VERIFYING → READY_TO_BOOT`, with `ERROR` on bad
size/CRC/order/timeout/abort. The device verifies every frame CRC, the final
stream CRC and the CRC of the bytes actually read back from flash. A version
lower than the running version is rejected.

Failure handling:

- slot A is not erased until slot B validates,
- an interrupted copy is retried on the next reset,
- after 3 failed attempts the state becomes `ERROR` and the old slot A still
  boots,
- metadata is written after verification and persisted with a double-page
  record store (no single point of failure during a write).

The complete state machine, reason codes and persisted record layouts are in
[docs/protocol.md](docs/protocol.md).

## Repository layout

```
firmware/
  common/      protocol, CRC, record store, metadata, install logic, filters
  bootloader/  validation, copy, jump, flash driver, main
  app/
    bsp/       time, GPIO, USART, I2C, ADC, flash (register level)
    drivers/   SHT30, ESP-AT client
    services/  sensor, WiFi, BLE, OTA
    app/       host link demux, main, JSON routing
    ld/        app.ld
  vendor/      CMSIS core, STM32C5xx device header, startup, system
cmake/         arm-none-eabi toolchain file
tests/         host unit tests + fake flash
tools/         firmware packer and provisioning/OTA host script
docs/          protocol, ESP-AT custom service, porting
```

## Build

Requires CMake ≥ 3.22, Ninja, and the **official Arm GNU Toolchain** (newlib
included). The Homebrew `arm-none-eabi-gcc` formula without newlib cannot link
this project.

Toolchain discovery, in order:

1. `ARM_TOOLCHAIN_BIN` environment variable,
2. `$HOME/tools/arm-gnu-toolchain-*/bin`,
3. `/Applications/ArmGNUToolchain/*/bin`, `/Applications/ARM/bin`,
4. `arm-none-eabi-gcc` on `PATH` (warning; may fail at link time).

Build firmware:

```bash
cmake --preset arm
cmake --build --preset arm
```

Artifacts:

```
build/arm/stm_ble_ota_bootloader.bin / .hex
build/arm/stm_ble_ota_app.bin / .hex
```

Reference sizes from this tree: bootloader 3,976 B flash / 5 KB RAM,
application 18,048 B flash / 9,432 B RAM. Both fit their regions.

Build and run host tests (native compiler):

```bash
cmake -S tests -B build-host
cmake --build build-host
./build-host/unit_tests
```

## Flash and debug (CLI only)

`STM32CubeProgrammer` CLI is the supported path:

```bash
STM32_Programmer_CLI -c port=SWD -w build/arm/stm_ble_ota_bootloader.bin 0x08000000 -v
STM32_Programmer_CLI -c port=SWD -w build/arm/stm_ble_ota_app.bin       0x08008000 -v
```

Mass erase first if the part is not blank:

```bash
STM32_Programmer_CLI -c port=SWD -e all
```

OpenOCD can be used instead, but an STM32C5 flash driver is not available in
all OpenOCD releases; verify before relying on it (KNOWN_ISSUES).

**No VS Code STM32 extension is required.** Building, flashing and OTA all run
from the command line. The optional Cortex-Debug extension only adds source
level debugging and is not needed for any workflow in this repository.

## Host tools

```bash
python3 -m pip install -r tools/requirements.txt
```

Package an image (adds version, hardware id and CRC to a `.ota` container):

```bash
python3 tools/firmware_tool.py inspect build/arm/stm_ble_ota_app.bin
python3 tools/firmware_tool.py pack build/arm/stm_ble_ota_app.bin \
    --version 0.1.0 --hw 0x0050 -o build/app-0.1.0.ota
```

Provisioning over the PC link:

```bash
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX info
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX wifi set \
    --ssid MyNetwork --password secret
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX wifi status
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX wifi clear
```

OTA end to end, including a reconnect/version check:

```bash
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX ota build/app-0.1.0.ota --verify
```

BLE mode (`bleak`, requires the custom ESP-AT service):

```bash
python3 tools/ota_host.py --ble --name STM-BLE-OTA info
python3 tools/ota_host.py --ble --name STM-BLE-OTA ota build/app-0.1.0.ota --verify
```

Serial is the recommended first bring-up path: it uses the same JSON commands
and the same binary OTA frames, with no dependence on the co-processor.

## Porting

`firmware/common`, the protocol logic and the sensor filter are portable C.
Changing MCU means touching the BSP, flash driver, linker scripts, startup and
`flash_layout.h`; [docs/porting.md](docs/porting.md) lists every file and the
flash-geometry constraints (single-bank parts need the RAM-resident erase
routine).

## Limitations

Not verified on hardware: boot, jump, flash erase/program, sensors, WiFi join,
BLE profile, and OTA over both transports. Clock is 48 MHz only; I2C timing,
ADC accuracy and flash timing are analytic, not measured. ESP-AT needs a custom
service table rebuild; stock firmware does not expose `0xFF00`. OTA has CRC
integrity but no signature or encryption. WiFi credentials are stored in
plaintext.

The complete list is in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Target facts and references

Verified against ST documentation during development:

- STM32C562RE: Cortex-M33 + FPU, 144 MHz, 512 KB dual-bank flash, 128 KB SRAM
  ([datasheet](https://www.st.com/resource/en/datasheet/stm32c562ce.pdf),
  [product page](https://www.st.com/en/microcontrollers-microprocessors/stm32c562re.html)).
- RM0522 *STM32C5 series reference manual*, V1.0, 27-Feb-2026 — flash
  architecture and register behaviour
  ([RM0522](https://www.st.com/resource/en/reference_manual/rm0522-stm32c5-series-armbased-32bit-mcus-stmicroelectronics.pdf)).
- STM32CubeC5 2.0.0, first official release 13-Mar-2026
  ([release notes](https://dev.st.com/stm32cube-docs/stm32c5-mcu-package/2.1.0/en/release_notes.html));
  STM32C5xx DFP 2.1.0, 12-Jun-2026
  ([release notes](https://dev.st.com/stm32cube-docs/stm32c5xx-dfp/2.1.0/en/release_notes.html)).
- ESP-AT custom BLE services use
  `customized_partitions/raw_data/ble_data/gatts_data.csv`
  ([ESP-AT guide](https://docs.espressif.com/projects/esp-at/en/latest/esp32/Compile_and_Develop/How_to_customize_BLE_services.html)).

## License

MIT, copyright 2026 platojobs. SPDX-License-Identifier: `MIT`. See
[LICENSE](LICENSE). Vendored CMSIS and STM32C5xx device files keep their own
licenses.
