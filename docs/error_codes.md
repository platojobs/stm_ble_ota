# Error codes

This document is the reference table for the shared, device-wide error-code
space declared in [`firmware/common/include/err_code.h`](../firmware/common/include/err_code.h).

The numeric value is part of the external contract:

- it is printed by the diagnostic CLI (`ota status` → `err_code` / `err_name`),
- it is what the error-log ring buffer stores and reports,
- host tools may map it back to a symbol with `err_code_name()`.

Existing codes are never renumbered. New entries are appended inside their
domain block and this table is updated in the same commit.

## Domains

| Range | Domain | Source modules |
| --- | --- | --- |
| `0x0000` | success | — |
| `0x01xx` | argument / framing / CLI | `ota_protocol`, `cli_parser`, `mini_json` |
| `0x02xx` | flash / record store | `bsp_flash`, `record_store`, `config_store`, `ota_metadata` |
| `0x03xx` | OTA transfer | `ota_receiver`, `ota_service` |
| `0x04xx` | image validation | `image_check`, `boot_install` |
| `0x05xx` | network / ESP-AT / BLE | `esp_at_parser`, `wifi_service`, `ble_service` |
| `0x06xx` | sensors / buses | `sht30`, `bsp_i2c`, `bsp_adc` |
| `0x07xx` | system / watchdog / boot | `boot_policy`, `bootloader`, self-test |

## Table

| Code | Symbol | Domain | Meaning |
| --- | --- | --- | --- |
| `0x0000` | `ERR_CODE_OK` | ok | Success |
| `0x0101` | `ERR_CODE_INVALID_ARG` | arg | Argument out of range or inconsistent |
| `0x0102` | `ERR_CODE_NULL_POINTER` | arg | Required pointer was `NULL` |
| `0x0103` | `ERR_CODE_BUFFER_TOO_SMALL` | arg | Output buffer cannot hold the result |
| `0x0104` | `ERR_CODE_MALFORMED_FRAME` | arg | Frame header/length is invalid |
| `0x0105` | `ERR_CODE_BAD_CRC` | arg | Payload CRC mismatch on a received frame |
| `0x0106` | `ERR_CODE_UNSUPPORTED` | arg | Command or frame type not implemented |
| `0x0107` | `ERR_CODE_LINE_TOO_LONG` | arg | CLI line exceeds `CLI_MAX_LINE` |
| `0x0201` | `ERR_CODE_FLASH_ERASE` | flash | Page erase failed |
| `0x0202` | `ERR_CODE_FLASH_PROGRAM` | flash | Word/double-word program failed |
| `0x0203` | `ERR_CODE_FLASH_READ` | flash | Read-back verification failed |
| `0x0204` | `ERR_CODE_STORE_EMPTY` | flash | No valid record in the store (factory state) |
| `0x0205` | `ERR_CODE_STORE_CORRUPT` | flash | Record magic/size/CRC invalid |
| `0x0206` | `ERR_CODE_STORE_FULL` | flash | No space left for another record |
| `0x0301` | `ERR_CODE_OTA_BAD_STATE` | ota | Command not allowed in the current state |
| `0x0302` | `ERR_CODE_OTA_BAD_SIZE` | ota | Image or payload size rejected |
| `0x0303` | `ERR_CODE_OTA_BAD_CRC` | ota | Final image CRC differs from the manifest |
| `0x0304` | `ERR_CODE_OTA_OUT_OF_ORDER` | ota | DATA offset does not continue the stream |
| `0x0305` | `ERR_CODE_OTA_TIMEOUT` | ota | Transfer stalled or link dropped |
| `0x0306` | `ERR_CODE_OTA_ABORTED` | ota | Host requested abort |
| `0x0307` | `ERR_CODE_OTA_BAD_VERSION` | ota | Version rejected (downgrade/format) |
| `0x0401` | `ERR_CODE_IMAGE_SIZE` | image | Image size outside the target slot |
| `0x0402` | `ERR_CODE_IMAGE_VECTOR` | image | Initial SP / reset vector not plausible |
| `0x0403` | `ERR_CODE_IMAGE_CRC` | image | Stored image CRC check failed |
| `0x0404` | `ERR_CODE_IMAGE_SHA256` | image | SHA-256 digest differs |
| `0x0405` | `ERR_CODE_IMAGE_SIGNATURE` | image | Ed25519 signature verification failed |
| `0x0501` | `ERR_CODE_AT_TIMEOUT` | network | ESP-AT command did not answer in time |
| `0x0502` | `ERR_CODE_AT_ERROR` | network | ESP-AT reported `ERROR` / unexpected URC |
| `0x0503` | `ERR_CODE_WIFI_NOT_PROVISIONED` | network | No SSID stored |
| `0x0504` | `ERR_CODE_WIFI_CONNECT_FAILED` | network | Join or DHCP failed |
| `0x0505` | `ERR_CODE_BLE_UNAVAILABLE` | network | ESP32 BLE stack not ready |
| `0x0601` | `ERR_CODE_I2C_ERROR` | sensor | Bus error / NACK |
| `0x0602` | `ERR_CODE_SENSOR_BUSY` | sensor | Sensor still converting |
| `0x0603` | `ERR_CODE_SENSOR_RANGE` | sensor | Reading outside the physical range |
| `0x0701` | `ERR_CODE_WATCHDOG_RESET` | system | Previous reset was an IWDG reset |
| `0x0702` | `ERR_CODE_BOOT_FAILED` | system | No bootable application |
| `0x0703` | `ERR_CODE_ROLLBACK` | system | Rolled back to the BACKUP image |
| `0x0704` | `ERR_CODE_NOT_IMPLEMENTED` | system | Feature reserved, not built |
| `0x0705` | `ERR_CODE_SELFTEST_FAILED` | system | Production self-test item failed |

## Mapping from internal enums

`ota_reason_t` (wire format, 8-bit) and `image_check_result_t` predate the
shared space. Their values stay unchanged on the wire for compatibility; the
translation helpers produce the device-wide code:

| `ota_reason_t` | Shared code |
| --- | --- |
| `OTA_REASON_NONE` | `ERR_CODE_OK` |
| `OTA_REASON_BAD_STATE` | `ERR_CODE_OTA_BAD_STATE` |
| `OTA_REASON_BAD_SIZE` | `ERR_CODE_OTA_BAD_SIZE` |
| `OTA_REASON_BAD_CRC` | `ERR_CODE_OTA_BAD_CRC` |
| `OTA_REASON_OUT_OF_ORDER` | `ERR_CODE_OTA_OUT_OF_ORDER` |
| `OTA_REASON_FLASH_ERROR` | `ERR_CODE_FLASH_PROGRAM` |
| `OTA_REASON_TIMEOUT` | `ERR_CODE_OTA_TIMEOUT` |
| `OTA_REASON_ABORTED` | `ERR_CODE_OTA_ABORTED` |
| `OTA_REASON_BAD_VERSION` | `ERR_CODE_OTA_BAD_VERSION` |
| `OTA_REASON_BAD_SIGNATURE` | `ERR_CODE_IMAGE_SIGNATURE` |

| `image_check_result_t` | Shared code |
| --- | --- |
| `IMAGE_OK` | `ERR_CODE_OK` |
| `IMAGE_ERR_SIZE` | `ERR_CODE_IMAGE_SIZE` |
| `IMAGE_ERR_VECTOR` | `ERR_CODE_IMAGE_VECTOR` |
| `IMAGE_ERR_CRC` | `ERR_CODE_IMAGE_CRC` |
| `IMAGE_ERR_FLASH` | `ERR_CODE_FLASH_READ` |
| `IMAGE_ERR_SHA256` | `ERR_CODE_IMAGE_SHA256` |
| `IMAGE_ERR_SIGNATURE` | `ERR_CODE_IMAGE_SIGNATURE` |

Unknown enum values are mapped to `ERR_CODE_INVALID_ARG`; unknown shared codes
render as `UNKNOWN` / domain `unknown`. Host-side unit tests
(`tests/test_err_codes.c`) fail if a table entry is missing, duplicated or a
mapping is incomplete.
