# Versioning and releases

## Semantic versioning

The project follows [SemVer 2.0.0](https://semver.org/): `MAJOR.MINOR.PATCH`.

- **MAJOR** — incompatible change for existing deployments: flash partition
  moves, bootloader metadata layout changes, `.ota` format changes, removal of
  a protocol field or an error-code renumbering.
- **MINOR** — backwards-compatible feature or protocol addition: a new CLI
  command, a new BLE characteristic field, a new error code, a new signed
  package feature.
- **PATCH** — backwards-compatible fix or documentation correction: bug fixes,
  bounds checks, static-analysis cleanups, wording.

While the project is at `0.x`, a minor bump may still contain a breaking
change (SemVer allows this); the changelog calls such changes out explicitly.
Do not rely on `0.x` compatibility for production without reading the
changelog.

## Encoding in the firmware

The OTA metadata, the image manifest and `APP_VERSION` use
`0x00MMmmpp` (`MAJOR << 16 | MINOR << 8 | PATCH`). It fits 24 bits; keep each
component ≤ 255. `CMakeLists.txt` derives the value from `FW_VERSION`
(`git describe` by default) and `tools/build.sh` injects it.

Bootloader and application must be built from the same tag for a release.
The bootloader validates the image's version but does not require it to match
its own.

## Tags and releases

- Tag format: `vX.Y.Z`, annotated, created only on `main`.
- A tag triggers CI, which builds both images, runs host tests and cppcheck,
  and creates a GitHub Release containing:
  - `stm_ble_ota_bootloader.bin` / `.hex`
  - `stm_ble_ota_app-<version>.ota` (signed when the secret exists)
  - `stm_ble_ota_app.bin` / `.hex`
  - `SHA256SUMS`
- Tags are never moved or reused. Never force-push over published history.
- `CHANGELOG.md` gets a dated section for every release before the tag is
  created.

## Pre-release versions

Use `-rc.N` / `-beta.N` suffixes when a release needs field testing
(`VERSION=0.3.0-rc.1`). The firmware encoding only carries three numeric
components, so `0.3.0-rc.1` is stored as `0.3.0`; the build id
(`FW_BUILD_ID`, default git commit count) distinguishes pre-release builds
in `version` output. Do not create a `vX.Y.Z` tag from an `-rc` build.
