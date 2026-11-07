# Contributing

Thanks for improving `stm_ble_ota`. This is a reference firmware tree; keep
changes small, buildable and documented.

## Before you start

- Read [KNOWN_ISSUES.md](KNOWN_ISSUES.md). Hardware-dependent work is
  explicitly marked as unverified — do not claim otherwise in a change.
- Read [docs/protocol.md](docs/protocol.md) and
  [docs/error_codes.md](docs/error_codes.md) before touching frames, OTA state
  or error values. Numeric codes and partitions are contracts.
- Never commit private keys, real WiFi credentials or CI secrets.
  `tools/keys/` is git-ignored.

## Build and test

Requirements: CMake ≥ 3.22, Ninja, Python 3, the official Arm GNU Toolchain
(see [docs/toolchain.md](docs/toolchain.md)).

```bash
# firmware (release-style, -Werror, signed package when SIGN_KEY is set)
VERSION=0.3.0 tools/build.sh

# host unit tests
cmake -S tests -B build-host -DWERROR=ON -G Ninja
cmake --build build-host
./build-host/unit_tests

# static analysis (same command as CI)
cppcheck --enable=warning,performance,portability --error-exitcode=1 \
  --inline-suppr --suppress=missingIncludeSystem --suppress=missingInclude \
  --std=c11 -I firmware/common/include -I firmware/app/include \
  -I firmware/bootloader/include -I firmware/vendor/stm32c5xx_dfp/Include \
  -I firmware/vendor/cmsis/Include \
  firmware/common/src firmware/app/src firmware/bootloader/src tests
```

A change is ready when both firmware images build with zero warnings and the
host suite passes. Add host tests for platform-independent logic (common
modules) and document what cannot be tested without hardware.

## Code style

- C11, no compiler extensions in `firmware/common`.
- 4 spaces, 4-space continuation, braces on the same line.
- Prefix module-private symbols with the file name (`ota_receiver_*`,
  `boot_policy_*`).
- Use the shared error space (`err_code.h`) for new failure paths; append new
  codes and update the table.
- Keep public headers self-contained and include what they use.
- Comments explain why; the existing SPDX header and copyright line are
  required at the top of every source file.

## Commits

- One logical change per commit; include tests and docs in the same commit.
- Write the message in the imperative mood, e.g.
  `feat: add resumable OTA progress`, `fix: bound credential length`.
- Reference the affected module when useful (`bootloader`, `ota`, `cli`).
- Do not rewrite published history and do not force push.

## Pull requests

Use the PR template. In the description state:

1. what changed and why,
2. how it was verified (commands, test counts, hardware if any),
3. which documented limitation or number it affects (partitions, sizes,
   protocol fields, error codes, version).

Update `CHANGELOG.md` for user-visible changes and bump the version according
to [docs/versioning.md](docs/versioning.md).
