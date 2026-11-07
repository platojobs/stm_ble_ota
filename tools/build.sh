#!/usr/bin/env bash
# One-shot firmware build + optional signed OTA package + checksums.
#
# Usage:
#   tools/build.sh                    # build bootloader + app
#   VERSION=1.2.3 tools/build.sh      # explicit version
#   SIGN_KEY=tools/keys/ed25519_private.pem tools/build.sh
#   BUILD_DIR=build/arm tools/build.sh
#
# Requirements: cmake >= 3.22, ninja, arm-none-eabi-gcc, python3,
#               cryptography (only when SIGN_KEY is set).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build/arm}"
GEN="${GEN:-Ninja}"
VERSION="${VERSION:-$(git -C "${ROOT}" describe --tags --always --dirty 2>/dev/null || echo 0.0.0)}"
VERSION="${VERSION#v}"

cmake -S "${ROOT}" -B "${BUILD_DIR}" -G "${GEN}" \
    -DCMAKE_TOOLCHAIN_FILE="${ROOT}/cmake/toolchain-arm-none-eabi.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DWERROR="${WERROR:-ON}" \
    -DFW_VERSION="${VERSION}"
cmake --build "${BUILD_DIR}"

APP_BIN="${BUILD_DIR}/stm_ble_ota_app.bin"
BOOT_BIN="${BUILD_DIR}/stm_ble_ota_bootloader.bin"
FILES=("${APP_BIN}" "${BOOT_BIN}" "${BUILD_DIR}/stm_ble_ota_app.hex" "${BUILD_DIR}/stm_ble_ota_bootloader.hex")

if [[ -n "${SIGN_KEY:-}" ]]; then
    python3 "${ROOT}/tools/firmware_tool.py" pack "${APP_BIN}" \
        --version "${VERSION}" --hw "${HW_ID:-0x0050}" \
        --build-id "${BUILD_ID:-$(git -C "${ROOT}" rev-list --count HEAD 2>/dev/null || echo 0)}" \
        --key "${SIGN_KEY}" \
        -o "${BUILD_DIR}/stm_ble_ota_app-${VERSION}.ota"
    FILES+=("${BUILD_DIR}/stm_ble_ota_app-${VERSION}.ota")
else
    echo "SIGN_KEY not set: skipping signed .ota package"
fi

(cd "${BUILD_DIR}" && sha256sum stm_ble_ota_app.bin stm_ble_ota_bootloader.bin \
    stm_ble_ota_app-*.ota 2>/dev/null > SHA256SUMS || \
    sha256sum stm_ble_ota_app.bin stm_ble_ota_bootloader.bin > SHA256SUMS)

echo "Build ${VERSION} complete:"
ls -l "${FILES[@]}"
