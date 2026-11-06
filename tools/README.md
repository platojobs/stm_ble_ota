# Host tools

Two small Python tools, no framework required.

```
firmware_tool.py   inspect / pack / unpack images
ota_host.py        provisioning and OTA over USART2 or BLE
requirements.txt   pyserial (required), bleak (BLE mode only)
```

Install:

```bash
python3 -m pip install -r tools/requirements.txt
```

## Firmware packaging

`firmware_tool.py` computes the same CRC-32/IEEE as the firmware and checks
that the image's initial SP and reset vector are plausible for slot A
(`0x0800_8000`).

```bash
python3 tools/firmware_tool.py inspect build/arm/stm_ble_ota_app.bin

python3 tools/firmware_tool.py pack build/arm/stm_ble_ota_app.bin \
    --version 0.2.0 --hw 0x0050 --build-id 2 -o build/app-0.2.0.ota

python3 tools/firmware_tool.py unpack build/app-0.2.0.ota -o build/app.bin
```

Package layout (24-byte header + image):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `OTAP` |
| 4 | 2 | format version (1) |
| 6 | 2 | hardware id |
| 8 | 4 | version `0x00MMmmpp` |
| 12 | 4 | image size |
| 16 | 4 | image CRC-32 |
| 20 | 4 | build id |
| 24 | N | image bytes |

## Provisioning and OTA

Serial mode uses the USART2 test link (ST-LINK virtual COM port). It carries
the same JSON messages and binary frames as the BLE path.

```bash
# read device info
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX info

# join an access point; credentials are persisted on the device
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX wifi set \
    --ssid MyNetwork --password secret

# read or erase provisioning
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX wifi status
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX wifi clear

# full OTA, then wait for the reset and read the new version back
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX \
    ota build/app-0.2.0.ota --verify
```

BLE mode requires the custom ESP-AT service described in
`../docs/esp32_at_custom_service.md`:

```bash
python3 tools/ota_host.py --ble --name STM-BLE-OTA info
python3 tools/ota_host.py --ble --name STM-BLE-OTA ota build/app-0.2.0.ota
```

Useful options: `--chunk` (DATA payload size, ≤ 512), `--pace` (delay between
DATA frames), `--timeout` (overall OTA timeout), `--reboot-timeout`,
`--address` (skip BLE scanning).

Progress is printed on stderr as JSON, so the stdout of an OTA run stays clean
for scripts. The process exits non-zero when the device reports an error state
or when a timeout expires.

## Expected sequence

```
BEGIN  -> progress state=receiving
DATA*  -> progress every 5 %
END    -> progress state=ready_to_boot
        device resets after 500 ms
boot   -> bootloader validates slot B, copies to slot A
        device restarts the application
--verify -> info shows the new version
```

Failure states are reported as `ota error reason=<name>`; reason codes are
listed in `../docs/protocol.md`.
