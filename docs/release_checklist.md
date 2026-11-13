# Release checklist

Scope: this repository's reference firmware (v0.4.0). Items marked **host** run
on a development machine; items marked **board** require hardware and are not
verified by this repository (see `KNOWN_ISSUES.md` and
[hil_test_plan.md](hil_test_plan.md)).

## 1. Code gate (host)

- [ ] Host tests pass without warnings:
      `cmake -S tests -B build-host -G Ninja -DWERROR=ON && cmake --build build-host && ./build-host/unit_tests`
- [ ] Both images build with `-Werror` and the size budget gate:
      `./tools/build.sh`
- [ ] Static analysis is clean:
      `cppcheck --enable=warning,performance,portability --error-exitcode=1 --inline-suppr --suppress=missingIncludeSystem --suppress=missingInclude --std=c11 -I firmware/common/include -I firmware/app/include -I firmware/bootloader/include firmware/common/src firmware/app/src firmware/bootloader/src tests`
- [ ] Host tools parse:
      `python3 -m py_compile tools/firmware_tool.py tools/ota_host.py tools/factory_test.py tools/check_size.py`
- [ ] `CHANGELOG.md` has an entry and `KNOWN_ISSUES.md` matches what was
      actually verified.
- [ ] Version, partition table and budgets in the READMEs match
      `flash_layout.h`, `app_config.h`, `tools/check_size.py`.

## 2. Image and package (host)

- [ ] Bootloader and application built from the same commit.
- [ ] Size report within budget (the build fails otherwise): app Flash 92 KB /
      RAM 24 KB, bootloader Flash 30 KB / RAM 8 KB.
- [ ] Production keypair used (not the reference key); public key baked into
      the bootloader and the bootloader re-flashed.
- [ ] Package signed with the release key and a security version **not lower**
      than the running one:
      `python3 tools/firmware_tool.py pack build/arm/stm_ble_ota_app.bin --version 0.4.0 --hw 0x0050 --build-id <id> --security-version <n> --key <private.pem> -o build/app-0.4.0.ota`
- [ ] `python3 tools/firmware_tool.py inspect build/app-0.4.0.ota` shows
      `package_format: 3`, the expected version, hardware id and security
      version.
- [ ] Archive the exact `.ota`, the map files and the size report with the
      release notes.

## 3. Unit production (board, not run here)

- [ ] Bootloader + application flashed, read back and checksummed.
- [ ] Serial programmed and read back:
      `serial set <sn>` then `serial`.
- [ ] One-shot production test passes and the result is exported:
      `python3 tools/factory_test.py --port <port> --serial <sn> --out build/factory/<sn>.json`
- [ ] `failed_mask` is `0`; 0x01 flash, 0x02 I2C/SHT30, 0x04 ADC, 0x08 BLE,
      0x10 serial, 0x20 crash history.
- [ ] Read-out protection / debug lock decision recorded.

## 4. Release gate (board, not run here)

- [ ] `HIL-01` … `HIL-12` in [hil_test_plan.md](hil_test_plan.md) executed.
- [ ] Result table attached to the release record; any skipped case is listed
      with a reason.
- [ ] Tag `v0.4.0` created from the tested commit and pushed:
      `git tag -a v0.4.0 -m "v0.4.0" && git push origin v0.4.0`.
