# Hardware-in-the-loop test plan

These cases must be executed on a NUCLEO-C562RE (or the target board) before a
release is declared hardware-verified. **None of them have been run by this
repository**; the status column records only what the host test suite covers.

## Bench setup

- Board with bootloader + application from the release commit.
- USART2 test link to the host (`tools/ota_host.py`, `tools/factory_test.py`),
  115200 8N1.
- BLE central for OTA (the host OTA tool), plus a signed `.ota` package.
- A second known-good package with a **lower** security version and one with a
  **different** hardware id for the rejection cases.
- Production fixture that can drive `serial set` and capture the JSON result.
- Power supply with a controllable reset (button or relay) for the
  crash/rollback/timeout cases.

## Cases

| ID | Case | Steps | Expected | Host coverage | Status |
| --- | --- | --- | --- | --- | --- |
| HIL-01 | Boot and identity | Flash both images, reset, `device` | Boots app, serial and hardware id reported, no crash record | record round-trip | Not run |
| HIL-02 | Serial programming | `serial set C562-000123`, reset, `serial` | Persists across reset; invalid characters rejected | normaliser + record store | Not run |
| HIL-03 | Production test pass | `factory test`, repeat via `factory_test.py` | `ok:true`, `failed_mask:0`, JSON exported | parser only | Not run |
| HIL-04 | Production test failure | Disconnect SHT30, run `factory test` | `ok:false`, bit 0x02 set, result still exported | — | Not run |
| HIL-05 | OTA happy path | Send a signed package, wait for reboot | Installs, version/security version increase, metadata `INSTALLED` | OTA logic tests | Not run |
| HIL-06 | Anti-rollback (APP) | Send a package with a lower `security_version` | Rejected with `bad_version`, running image unchanged | unit test | Not run |
| HIL-07 | Anti-rollback (bootloader) | Stage a lower-security image, reboot into the bootloader | Bootloader refuses to install, boots slot A | unit test | Not run |
| HIL-08 | Hardware mismatch | Send a package with hardware id `0x0052` | Rejected with `bad_hardware` (10), no flash write | unit test | Not run |
| HIL-09 | Session timeout | Start a transfer, stop sending, wait > 70 s, reboot | 10 s suspend keeps prefix; after 60 s the session aborts, `received_bytes=0`, next BEGIN starts at offset 0 | unit test | Not run |
| HIL-10 | Crash persistence | Force a fault (debugger write to invalid address), reset | Crash record readable via `crash` and BLE, survives reset, clears with `crash clear` | unit test | Not run |
| HIL-11 | Stack/resource report | Run `resource` at boot and after traffic | `canary_ok:true`, high-water grows, heap peak plausible, RAM margin ≥ 2 KB | unit test | Not run |
| HIL-12 | Rollback on failed boot | Install an image that fails its boot confirmation | Bootloader restores the backup and boots it | unit test | Not run |

## Recording results

- Attach the `factory_test.py` JSON files and the HIL result table to the
  release record.
- Any case not executed must be listed as **not verified** in
  `KNOWN_ISSUES.md`; do not describe it as passing.
