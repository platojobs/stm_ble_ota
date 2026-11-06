#!/usr/bin/env python3
"""Firmware package helper for the stm_ble_ota reference project.

Two artifact kinds are understood:

  .bin   raw application image produced by objcopy from stm_ble_ota_app.elf.
         The image is linked for slot A (0x0800_8000) but is stored in slot B
         while downloading, so the vector table is checked against slot A.

  .ota   self-describing package consumed by ota_host.py:

         offset  size  field
         0       4     magic "OTAP"
         4       2     format version (1)
         6       2     hardware id (0x0050 for the NUCLEO-C562RE profile)
         8       4     image version, 0x00MMmmpp
         12      4     image size in bytes
         16      4     CRC-32/IEEE of the image
         20      4     build id (free-form, printed by the device info JSON)
         24      N     image bytes

CRC-32 here is the same reflected IEEE polynomial (0xEDB88320) used by
firmware/common/src/crc.c, so zlib.crc32()/binascii.crc32() are compatible.

Examples:
    python3 tools/firmware_tool.py inspect build/stm_ble_ota_app.bin
    python3 tools/firmware_tool.py pack build/stm_ble_ota_app.bin \
        --version 0.2.0 --hw 0x0050 --build-id 1 -o build/app-0.2.0.ota
    python3 tools/firmware_tool.py unpack build/app-0.2.0.ota -o build/app.bin
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

PACKAGE_MAGIC = b"OTAP"
PACKAGE_FORMAT = 1
PACKAGE_HEADER = struct.Struct("<4sHHIIII")  # magic, format, hw, version, size, crc, build_id
HEADER_SIZE = PACKAGE_HEADER.size  # 24 bytes

SLOT_A_START = 0x08008000
SLOT_A_SIZE = 128 * 1024
SRAM_START = 0x20000000
SRAM_SIZE = 128 * 1024


def parse_version(text: str) -> int:
    """Accept 0.2.0, 0x00020000 or a plain integer and return 0x00MMmmpp."""
    text = text.strip()
    if "." in text:
        parts = text.split(".")
        if len(parts) > 3:
            raise argparse.ArgumentTypeError(f"invalid version: {text}")
        major, minor, patch = (int(p, 0) for p in (parts + ["0", "0"])[:3])
        if not all(0 <= v <= 255 for v in (major, minor, patch)):
            raise argparse.ArgumentTypeError(f"version component out of range: {text}")
        return (major << 16) | (minor << 8) | patch
    value = int(text, 0)
    if not 0 <= value <= 0xFFFFFF:
        raise argparse.ArgumentTypeError(f"version out of range: {text}")
    return value


def version_string(value: int) -> str:
    return f"{(value >> 16) & 0xFF}.{(value >> 8) & 0xFF}.{value & 0xFF}"


def read_image(path: Path) -> bytes:
    data = path.read_bytes()
    if not data:
        raise SystemExit(f"{path}: empty file")
    return data


def image_report(data: bytes, version: int | None, hardware_id: int | None,
                 build_id: int | None) -> dict:
    """Common summary plus a vector-table sanity check for a slot A image."""
    size = len(data)
    crc = zlib.crc32(data) & 0xFFFFFFFF
    if size >= 8:
        initial_sp, reset_vector = struct.unpack_from("<II", data, 0)
        reset_address = reset_vector & ~1
        vector_ok = (SRAM_START <= initial_sp <= SRAM_START + SRAM_SIZE
                     and SLOT_A_START <= reset_address < SLOT_A_START + SLOT_A_SIZE)
    else:
        initial_sp = reset_vector = reset_address = 0
        vector_ok = False

    report: dict = {
        "image_size": size,
        "image_crc32": crc,
        "image_crc32_hex": f"0x{crc:08X}",
        "sha256": hashlib.sha256(data).hexdigest(),
        "initial_sp": f"0x{initial_sp:08X}",
        "reset_vector": f"0x{reset_vector:08X}",
        "reset_address": f"0x{reset_address:08X}",
        "vector_table_ok": vector_ok,
        "fits_slot_a": size <= SLOT_A_SIZE,
    }
    if version is not None:
        report["version"] = version_string(version)
        report["version_raw"] = version
    if hardware_id is not None:
        report["hardware_id"] = hardware_id
    if build_id is not None:
        report["build_id"] = build_id
    return report


def cmd_inspect(args: argparse.Namespace) -> int:
    data = read_image(args.image)
    version = hardware_id = build_id = None
    if data[:4] == PACKAGE_MAGIC:
        magic, fmt, hardware_id, version, size, crc, build_id = \
            PACKAGE_HEADER.unpack_from(data, 0)
        image = data[HEADER_SIZE:]
        if fmt != PACKAGE_FORMAT:
            raise SystemExit(f"unsupported package format {fmt}")
        if size != len(image):
            raise SystemExit(f"package size mismatch: header {size}, payload {len(image)}")
        if crc != (zlib.crc32(image) & 0xFFFFFFFF):
            raise SystemExit("package CRC mismatch")
        report = image_report(image, version, hardware_id, build_id)
        report["package_format"] = fmt
    else:
        report = image_report(data, None, None, None)

    print(json.dumps(report, indent=2))
    if not report["vector_table_ok"]:
        print("warning: vector table does not look like a slot A image",
              file=sys.stderr)
        return 1
    return 0


def cmd_pack(args: argparse.Namespace) -> int:
    image = read_image(args.image)
    report = image_report(image, args.version, args.hw, args.build_id)
    if not report["vector_table_ok"]:
        print("error: vector table does not look like a slot A image", file=sys.stderr)
        return 1
    if not report["fits_slot_a"]:
        print(f"error: image is {len(image)} bytes, slot A holds {SLOT_A_SIZE}",
              file=sys.stderr)
        return 1

    header = PACKAGE_HEADER.pack(PACKAGE_MAGIC, PACKAGE_FORMAT, args.hw,
                                 args.version, len(image), report["image_crc32"],
                                 args.build_id)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + image)
    print(json.dumps({
        "output": str(args.output),
        "version": version_string(args.version),
        **{k: report[k] for k in
           ("image_size", "image_crc32_hex", "hardware_id", "build_id")},
    }, indent=2))
    return 0


def cmd_unpack(args: argparse.Namespace) -> int:
    data = read_image(args.package)
    if data[:4] != PACKAGE_MAGIC:
        raise SystemExit(f"{args.package}: not an OTAP package")
    magic, fmt, hardware_id, version, size, crc, build_id = \
        PACKAGE_HEADER.unpack_from(data, 0)
    image = data[HEADER_SIZE:]
    if fmt != PACKAGE_FORMAT or size != len(image):
        raise SystemExit("package header is inconsistent")
    if crc != (zlib.crc32(image) & 0xFFFFFFFF):
        raise SystemExit("package CRC mismatch")
    args.output.write_bytes(image)
    print(json.dumps({
        "output": str(args.output),
        "version": version_string(version),
        "hardware_id": hardware_id,
        "build_id": build_id,
        "image_size": size,
    }, indent=2))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    inspect = sub.add_parser("inspect", help="print size/CRC/vector table of an image")
    inspect.add_argument("image", type=Path)
    inspect.set_defaults(func=cmd_inspect)

    pack = sub.add_parser("pack", help="wrap a .bin into an OTAP package")
    pack.add_argument("image", type=Path)
    pack.add_argument("--version", type=parse_version, required=True,
                      help="0.2.0 or 0x00020000")
    pack.add_argument("--hw", type=lambda v: int(v, 0), default=0x0050,
                      help="hardware id (default 0x0050)")
    pack.add_argument("--build-id", type=lambda v: int(v, 0), default=0,
                      help="free-form build identifier")
    pack.add_argument("-o", "--output", type=Path, required=True)
    pack.set_defaults(func=cmd_pack)

    unpack = sub.add_parser("unpack", help="extract the .bin from an OTAP package")
    unpack.add_argument("package", type=Path)
    unpack.add_argument("-o", "--output", type=Path, required=True)
    unpack.set_defaults(func=cmd_unpack)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
