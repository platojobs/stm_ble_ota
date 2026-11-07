# Toolchain and reproducible builds

All build steps are command-line only. No IDE plugin, no vendor code generator.

## Tested versions

| Tool | Version used for v0.2.0 | Minimum | Notes |
| --- | --- | --- | --- |
| Arm GNU Toolchain (`arm-none-eabi-gcc`) | 15.3.rel2 (official .pkg/.tar.xz) | 13.2 | must ship newlib; Homebrew's bare formula does not link |
| CMake | 3.31 | 3.22 | presets require 3.22 |
| Ninja | 1.12 | 1.10 | Makefiles also work |
| Python | 3.13 | 3.10 | host tools only |
| cryptography | 42 | 41 | package signing |
| cppcheck | 2.18 | 2.13 | CI static analysis |
| pyserial | 3.5 | 3.4 | serial host mode |
| bleak | 0.22 | 0.22 | BLE host mode |

CI uses the Ubuntu `gcc-arm-none-eabi` package (currently 13.2.1) and prints
the exact versions in the build log. A local build with a different compiler
may produce different code sizes; the map files are the reference.

## Discovery order

`cmake/toolchain-arm-none-eabi.cmake` resolves the compiler in this order:

1. `ARM_TOOLCHAIN_BIN` environment variable (`.../bin`),
2. `~/tools/arm-gnu-toolchain-*/bin`,
3. `/Applications/ArmGNUToolchain/*/bin`, `/Applications/ARM/bin`,
4. `arm-none-eabi-gcc` on `PATH`.

A PATH fallback prints a warning because Homebrew builds without newlib fail
at link time.

## Reproducing a release

```bash
# pinned toolchain via ARM_TOOLCHAIN_BIN
export ARM_TOOLCHAIN_BIN="$HOME/tools/arm-gnu-toolchain-15.3.rel2/bin"
# explicit version and build id, so the binary does not depend on git state
VERSION=0.3.0 BUILD_ID=20 \
  SIGN_KEY=/secure/ota_signing_key.pem \
  tools/build.sh
```

The script writes `SHA256SUMS` next to the binaries. CI does the same and
attaches the signed `.ota` to the GitHub Release for tags `v*`.

## Version injection

CMake reads `git describe --tags --always --dirty` when `-DFW_VERSION` is not
given, strips a leading `v`, and accepts `MAJOR.MINOR.PATCH` (extra
`-N-gHASH-dirty` suffixes are ignored for the numeric value). It then defines
`APP_VERSION`, `APP_VERSION_STRING` and `APP_BUILD_ID` for the application.
Override explicitly when building from an export without `.git`:

```bash
cmake -S . -B build/arm -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake \
  -DFW_VERSION=0.3.0 -DFW_BUILD_ID=20 -DWERROR=ON
```
