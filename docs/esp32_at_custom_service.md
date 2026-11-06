# Custom ESP-AT GATT service for this project

The STM32 exposes a BLE peripheral through an ESP32 running ESP-AT in GATT
server mode. Stock ESP-AT builds only contain the services compiled into its
factory BLE table, so the `0xFF00` profile used here must be added to that
table and the BLE data partition rebuilt.

This document describes what the firmware expects. It has **not** been
verified on hardware; see `KNOWN_ISSUES.md`.

## 1. Where the table lives

In an ESP-AT checkout the BLE service source file is:

```
customized_partitions/raw_data/ble_data/gatts_data.csv
```

It is a CSV table of GATT attributes. Relevant columns:

| Column | Meaning |
| --- | --- |
| `index` | row number, must continue the file's existing numbering |
| `uuid_len` | UUID size in **bits** (`16` for 16-bit UUIDs) |
| `uuid` | attribute UUID, e.g. `0x2800` primary service, `0x2803` characteristic declaration, `0x2902` CCCD |
| `perm` | attribute permissions: `0x01` read, `0x10` write, `0x11` read+write |
| `val_max_len` | maximum value length in bytes |
| `val_cur_len` | initial value length in bytes |
| `value` | initial value, hex, may be empty |

Characteristic properties byte (the `value` of a `0x2803` row):

| Bit | Property |
| --- | --- |
| 1 | read (0x02) |
| 2 | write without response (0x04) |
| 3 | write (0x08) |
| 4 | notify (0x10) |

## 2. Rows to append

One primary service `0xFF00`, five characteristics. `index` values below are
placeholders: continue from the last row in your `gatts_data.csv`. The
relative order matters, because ESP-AT assigns the AT service/characteristic
indices from it.

| index | uuid_len | uuid | perm | val_max_len | val_cur_len | value |
| --- | --- | --- | --- | --- | --- | --- |
| N   | 16 | 0x2800 | 0x01 | 2 | 2 | FF00 |
| N+1 | 16 | 0x2803 | 0x01 | 1 | 1 | 12 |
| N+2 | 16 | 0xFF01 | 0x01 | 64 | 0 | |
| N+3 | 16 | 0x2902 | 0x11 | 2 | 2 | 0000 |
| N+4 | 16 | 0x2803 | 0x01 | 1 | 1 | 10 |
| N+5 | 16 | 0xFF02 | 0x01 | 20 | 0 | |
| N+6 | 16 | 0x2902 | 0x11 | 2 | 2 | 0000 |
| N+7 | 16 | 0x2803 | 0x01 | 1 | 1 | 18 |
| N+8 | 16 | 0xFF03 | 0x10 | 64 | 0 | |
| N+9 | 16 | 0x2902 | 0x11 | 2 | 2 | 0000 |
| N+10 | 16 | 0x2803 | 0x01 | 1 | 1 | 18 |
| N+11 | 16 | 0xFF04 | 0x10 | 64 | 0 | |
| N+12 | 16 | 0x2902 | 0x11 | 2 | 2 | 0000 |
| N+13 | 16 | 0x2803 | 0x01 | 1 | 1 | 08 |
| N+14 | 16 | 0xFF05 | 0x10 | 64 | 0 | |

As raw CSV:

```csv
N,16,0x2800,0x01,2,2,FF00
N+1,16,0x2803,0x01,1,1,12
N+2,16,0xFF01,0x01,64,0,
N+3,16,0x2902,0x11,2,2,0000
N+4,16,0x2803,0x01,1,1,10
N+5,16,0xFF02,0x01,20,0,
N+6,16,0x2902,0x11,2,2,0000
N+7,16,0x2803,0x01,1,1,18
N+8,16,0xFF03,0x10,64,0,
N+9,16,0x2902,0x11,2,2,0000
N+10,16,0x2803,0x01,1,1,18
N+11,16,0xFF04,0x10,64,0,
N+12,16,0x2902,0x11,2,2,0000
N+13,16,0x2803,0x01,1,1,08
N+14,16,0xFF05,0x10,64,0,
```

`val_max_len` is 64 rather than 512 for the OTA characteristics because ATT
writes are limited by the MTU (20 bytes with the default MTU 23). The MCU
reassembles whole 528-byte frames across many writes, so the attribute itself
never needs to hold a full frame.

Properties: characteristic 0 is read+notify (`12`), 1 is notify (`10`),
2 and 3 are write+notify (`18`), 4 is write (`08`). Every notify or write
characteristic that may be enabled by the peer needs the `0x2902` CCCD row.

## 3. AT indices used by the firmware

`firmware/app/include/app_config.h` assumes:

- service index `1`,
- characteristic indices `0..4` in the order above.

Service index `1` assumes the stock table already contains one primary
service (typically the Device Information Service) at index `0`. Verify the
actual indices on your ESP32 with:

```
AT+BLEGATTSSRVCRE
AT+BLEGATTSCHAR?
```

If your table produces different numbers, change only `BLE_SERVICE_INDEX` and
`BLE_CHAR_*` in `app_config.h`; no other code is index-dependent.

## 4. Build and flash the BLE data partition

1. Edit `customized_partitions/raw_data/ble_data/gatts_data.csv` as above.
2. Regenerate the BLE data / `mfg_nvs.bin` partition with the generator shipped
   in the ESP-AT release you use (older releases expose
   `tools/BLEService.py`; newer ones fold BLE services into the mfg NVS
   generator). The exact script name changes between ESP-AT releases; the
   official procedure is *How to customize BLE services* / *How to update
   mfg_nvs* in the ESP-AT User Guide.
3. Flash the generated partition at the address reported by the generator.
4. Flash the ESP-AT application binary as usual.

Then the MCU can use `AT+BLEGATTSSRVCRE` / `AT+BLEGATTSSRVSTART` with the
custom service already present.

## 5. Alternative without rebuilding ESP-AT

If rebuilding ESP-AT is not acceptable, the MCU-side BLE path cannot be
exercised with the custom profile. In that case use the USART2 PC link
(`tools/ota_host.py --serial ...`), which carries exactly the same JSON
commands and OTA frames and does not depend on the co-processor at all.
