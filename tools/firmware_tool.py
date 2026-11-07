#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 platojobs

"""Firmware package helper for the stm_ble_ota reference project.

Two artifact kinds are understood:

  .bin   raw application image produced by objcopy from stm_ble_ota_app.elf.
         The image is linked for slot A (0x0800_8000) but is stored in slot B
         while downloading, so the vector table is checked against slot A.

  .ota   self-describing package consumed by ota_host.py:

         offset  size  field
         0       4     magic "OTAP"
         4       2     format version (2)
         6       2     hardware id (0x0050 for the NUCLEO-C562RE profile)
         8       4     image version, 0x00MMmmpp
         12      4     image size in bytes
         16      4     CRC-32/IEEE of the image
         20      4     build id (free-form, printed by the device info JSON)
         24      32    image SHA-256
         56      64    Ed25519 signature over (sha256, size, version,
                            build_id, hardware_id) little-endian
         120     N     image bytes

CRC-32 here is the same reflected IEEE polynomial (0xEDB88320) used by
firmware/common/src/crc.c, so zlib.crc32()/binascii.crc32() are compatible.

Signature (v2 packages only): the Ed25519 private key is passed with --key
(default tools/keys/demo_ed25519_private.pem). The matching public key is
compiled into the bootloader (firmware/common/include/ota_public_key.h).
Replace the demo keypair with an offline-generated one for production and never
commit the production private key.

Examples:
    python3 tools/firmware_tool.py inspect build/stm_ble_ota_app.bin
    python3 tools/firmware_tool.py keygen -o tools/keys/my_ed25519_private.pem
    python3 tools/firmware_tool.py pack build/stm_ble_ota_app.bin \\
        --version 0.2.0 --hw 0x0050 --build-id 1 \\
        --key tools/keys/demo_ed25519_private.pem -o build/app-0.2.0.ota
    python3 tools/firmware_tool.py unpack build/app-0.2.0.ota -o build/app.bin
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
import zlib
from pathlib import Path

PACKAGE_MAGIC = b"OTAP"
PACKAGE_FORMAT = 2
HEADER_V1 = struct.Struct("<4sHHIIII")  # 24 bytes
HEADER_V2 = struct.Struct("<4sHHIIII32s64s")  # 120 bytes
HEADER_SIZE = HEADER_V2.size

SLOT_A_START = 0x08008000
SLOT_A_SIZE = 96 * 1024
SRAM_START = 0x20000000
SRAM_SIZE = 128 * 1024

DEMO_KEY = Path(__file__).resolve().parent / "keys" / "demo_ed25519_private.pem"

# Canonical signed blob: 32 bytes sha256 + size(u32) + version(u32) +
# build_id(u32) + hardware_id(u16) + reserved(u16), little-endian.
SIGNED_BLOB = struct.Struct("<32sIIIHH")


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


def load_private_key(path: Path):
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric.ed25519 import \
            Ed25519PrivateKey
    except ImportError as exc:  # pragma: no cover
        raise SystemExit(
            "cryptography is required for signing: "
            "pip install -r tools/requirements.txt") from exc
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise SystemExit(f"{path}: not an Ed25519 private key")
    return key


def public_key_pem(private_key) -> bytes:
    from cryptography.hazmat.primitives import serialization
    return private_key.public_key().public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo)


def sign_image(image: bytes, version: int, hardware_id: int, build_id: int,
               private_key) -> tuple[bytes, bytes]:
    """Return (sha256, signature) for the canonical descriptor blob."""
    digest = hashlib.sha256(image).digest()
    blob = SIGNED_BLOB.pack(digest, len(image), version, build_id, hardware_id, 0)
    signature = private_key.sign(blob)
    if len(signature) != 64:
        raise SystemExit("Ed25519 signature must be 64 bytes")
    return digest, signature


def image_report(data: bytes, version: int | None, hardware_id: int | None,
                 build_id: int | None, sha256: bytes | None = None,
                 signature: bytes | None = None) -> dict:
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

    digest = sha256 if sha256 is not None else hashlib.sha256(data).digest()
    report: dict = {
        "image_size": size,
        "image_crc32": crc,
        "image_crc32_hex": f"0x{crc:08X}",
        "sha256": digest.hex(),
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
    if signature is not None:
        report["signature"] = signature.hex()
    return report


def cmd_inspect(args: argparse.Namespace) -> int:
    data = read_image(args.image)
    if data[:4] == PACKAGE_MAGIC:
        fmt = struct.unpack_from("<H", data, 4)[0]
        if fmt == 1:
            magic, fmt, hardware_id, version, size, crc, build_id = \
                HEADER_V1.unpack_from(data, 0)
            image = data[HEADER_V1.size:]
            sha = sig = None
        elif fmt == PACKAGE_FORMAT:
            (magic, fmt, hardware_id, version, size, crc, build_id,
             sha, sig) = HEADER_V2.unpack_from(data, 0)
            image = data[HEADER_SIZE:]
        else:
            raise SystemExit(f"unsupported package format {fmt}")
        if size != len(image):
            raise SystemExit(f"package size mismatch: header {size}, payload {len(image)}")
        if crc != (zlib.crc32(image) & 0xFFFFFFFF):
            raise SystemExit("package CRC mismatch")
        report = image_report(image, version, hardware_id, build_id, sha, sig)
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

    key_path = (Path(os.environ["OTA_SIGNING_KEY"])
                if "OTA_SIGNING_KEY" in os.environ else args.key)
    if key_path is None:
        print("error: --key or OTA_SIGNING_KEY is required to sign packages",
              file=sys.stderr)
        return 1
    key = load_private_key(key_path)
    digest, signature = sign_image(image, args.version, args.hw, args.build_id, key)
    report["sha256"] = digest.hex()
    report["signature"] = signature.hex()

    header = HEADER_V2.pack(PACKAGE_MAGIC, PACKAGE_FORMAT, args.hw, args.version,
                            len(image), report["image_crc32"], args.build_id,
                            digest, signature)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + image)
    print(json.dumps({
        "output": str(args.output),
        "version": version_string(args.version),
        **{k: report[k] for k in
           ("image_size", "image_crc32_hex", "hardware_id", "build_id",
            "sha256", "signature")},
    }, indent=2))
    return 0


def cmd_unpack(args: argparse.Namespace) -> int:
    data = read_image(args.package)
    if data[:4] != PACKAGE_MAGIC:
        raise SystemExit(f"{args.package}: not an OTAP package")
    fmt = struct.unpack_from("<H", data, 4)[0]
    if fmt == 1:
        magic, fmt, hardware_id, version, size, crc, build_id = \
            HEADER_V1.unpack_from(data, 0)
        image = data[HEADER_V1.size:]
        sha = sig = None
    elif fmt == PACKAGE_FORMAT:
        (magic, fmt, hardware_id, version, size, crc, build_id,
         sha, sig) = HEADER_V2.unpack_from(data, 0)
        image = data[HEADER_SIZE:]
    else:
        raise SystemExit(f"{args.package}: unsupported package format {fmt}")
    if size != len(image):
        raise SystemExit("package header size mismatch")
    if crc != (zlib.crc32(image) & 0xFFFFFFFF):
        raise SystemExit("package CRC mismatch")
    args.output.write_bytes(image)
    result = {
        "output": str(args.output),
        "version": version_string(version),
        "hardware_id": hardware_id,
        "build_id": build_id,
        "image_size": size,
    }
    if sha is not None:
        result["sha256"] = sha.hex()
    print(json.dumps(result, indent=2))
    return 0


def cmd_keygen(args: argparse.Namespace) -> int:
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric.ed25519 import \
            Ed25519PrivateKey
    except ImportError as exc:  # pragma: no cover
        raise SystemExit(
            "cryptography is required: pip install -r tools/requirements.txt") \
            from exc
    key = Ed25519PrivateKey.generate()
    pem = key.private_bytes(serialization.Encoding.PEM,
                            serialization.PrivateFormat.PKCS8,
                            serialization.NoEncryption())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(pem)
    pub = key.public_key().public_bytes(serialization.Encoding.PEM,
                                        serialization.PublicFormat.SubjectPublicKeyInfo)
    if args.public:
        args.public.write_bytes(pub)
    print(json.dumps({
        "private": str(args.output),
        "public": str(args.public) if args.public else None,
        "note": "keep the private key offline; only the public key goes into the bootloader",
    }, indent=2))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    inspect = sub.add_parser("inspect", help="print size/CRC/vector table of an image")
    inspect.add_argument("image", type=Path)
    inspect.set_defaults(func=cmd_inspect)

    pack = sub.add_parser("pack", help="wrap a .bin into a signed OTAP package")
    pack.add_argument("image", type=Path)
    pack.add_argument("--version", type=parse_version, required=True,
                      help="0.2.0 or 0x00020000")
    pack.add_argument("--hw", type=lambda v: int(v, 0), default=0x0050,
                      help="hardware id (default 0x0050)")
    pack.add_argument("--build-id", type=lambda v: int(v, 0), default=0,
                      help="free-form build identifier")
    pack.add_argument("--key", type=Path,
                      default=Path(__file__).resolve().parent / "keys" /
                              "demo_ed25519_private.pem",
                      help="Ed25519 private key PEM (or set OTA_SIGNING_KEY); "
                           "defaults to the committed demo keypair — replace "
                           "with an offline key for production")
    pack.add_argument("-o", "--output", type=Path, required=True)
    pack.set_defaults(func=cmd_pack)

    unpack = sub.add_parser("unpack", help="extract the .bin from an OTAP package")
    unpack.add_argument("package", type=Path)
    unpack.add_argument("-o", "--output", type=Path, required=True)
    unpack.set_defaults(func=cmd_unpack)

    keygen = sub.add_parser("keygen", help="generate a new Ed25519 keypair")
    keygen.add_argument("-o", "--output", type=Path, required=True,
                        help="private key PEM output")
    keygen.add_argument("--public", type=Path, default=None,
                        help="also write the public key PEM")
    keygen.set_defaults(func=cmd_keygen)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
