#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 platojobs

"""Minimal host tool for provisioning and OTA against stm_ble_ota.

Two transports are supported:

  serial  USART2 on the ST-LINK virtual COM port (PA2/PA3, 115200 8N1).
          Text JSON commands and binary OTA frames share the same link.

  ble     The custom GATT service exposed by the ESP32 co-processor.
          Requires the ESP-AT firmware described in
          docs/esp32_at_custom_service.md and the `bleak` package.

Serial examples:
    python3 tools/ota_host.py --serial /dev/cu.usbmodem14201 info
    python3 tools/ota_host.py --serial /dev/cu.usbmodem14201 wifi status
    python3 tools/ota_host.py --serial /dev/cu.usbmodem14201 wifi set \
        --ssid MyNetwork --password secret
    python3 tools/ota_host.py --serial /dev/cu.usbmodem14201 ota build/app.ota

BLE examples:
    python3 tools/ota_host.py --ble --name STM-BLE-OTA info
    python3 tools/ota_host.py --ble --name STM-BLE-OTA wifi set \
        --ssid MyNetwork --password secret
    python3 tools/ota_host.py --ble --name STM-BLE-OTA ota build/app.ota

The OTA transfer is the same in both modes: BEGIN, DATA (512-byte payloads),
END, then the device persists READY_TO_BOOT, resets, and the bootloader copies
slot B to slot A. Use `--verify` to wait for the reboot and read the new
version back.
"""

from __future__ import annotations

import argparse
import asyncio
import binascii
import json
import struct
import sys
import time
import zlib
from pathlib import Path

SYNC0 = 0xA5
SYNC1 = 0x5A
HEADER_SIZE = 16
MAX_PAYLOAD = 512

FRAME_CMD_JSON = 0x01
FRAME_OTA_BEGIN = 0x10
FRAME_OTA_DATA = 0x11
FRAME_OTA_END = 0x12
FRAME_OTA_ABORT = 0x13
FRAME_OTA_PROGRESS = 0x14
FRAME_OTA_RESULT = 0x15

STATE_IDLE = 0
STATE_RECEIVING = 1
STATE_VERIFYING = 2
STATE_READY_TO_BOOT = 3
STATE_INSTALLING = 4
STATE_INSTALLED = 5
STATE_ERROR = 6

OTA_STATE_NAMES = {
    STATE_IDLE: "idle",
    STATE_RECEIVING: "receiving",
    STATE_VERIFYING: "verifying",
    STATE_READY_TO_BOOT: "ready_to_boot",
    STATE_INSTALLING: "installing",
    STATE_INSTALLED: "installed",
    STATE_ERROR: "error",
}

REASON_NAMES = {
    0: "none",
    1: "bad_state",
    2: "bad_size",
    3: "bad_crc",
    4: "out_of_order",
    5: "flash_error",
    6: "timeout",
    7: "aborted",
    8: "bad_version",
}

PACKAGE_MAGIC = b"OTAP"
PACKAGE_HEADER = struct.Struct("<4sHHIIII")

SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
CHAR_INFO = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_SENSOR = "0000ff02-0000-1000-8000-00805f9b34fb"
CHAR_PROVISION = "0000ff03-0000-1000-8000-00805f9b34fb"
CHAR_OTA_CONTROL = "0000ff04-0000-1000-8000-00805f9b34fb"
CHAR_OTA_DATA = "0000ff05-0000-1000-8000-00805f9b34fb"

SLOT_A_START = 0x08008000
SLOT_A_SIZE = 128 * 1024
SRAM_START = 0x20000000
SRAM_SIZE = 128 * 1024


class ToolError(RuntimeError):
    pass


# --------------------------------------------------------------------------
# Image and frame helpers
# --------------------------------------------------------------------------

def version_string(value: int) -> str:
    return f"{(value >> 16) & 0xFF}.{(value >> 8) & 0xFF}.{value & 0xFF}"


def load_image(path: Path) -> dict:
    """Load a .bin or .ota file and return image bytes plus metadata."""
    raw = path.read_bytes()
    if not raw:
        raise ToolError(f"{path}: empty file")

    meta = {"image": raw, "version": 0, "hardware_id": 0x0050, "build_id": 0,
            "source": str(path)}
    if raw[:4] == PACKAGE_MAGIC:
        magic, fmt, hardware_id, version, size, crc, build_id = \
            PACKAGE_HEADER.unpack_from(raw, 0)
        image = raw[PACKAGE_HEADER.size:]
        if fmt != 1:
            raise ToolError(f"{path}: unsupported package format {fmt}")
        if size != len(image) or crc != (zlib.crc32(image) & 0xFFFFFFFF):
            raise ToolError(f"{path}: package header/CRC mismatch")
        meta.update(image=image, version=version, hardware_id=hardware_id,
                    build_id=build_id, package_crc=crc)

    image = meta["image"]
    if len(image) > SLOT_A_SIZE:
        raise ToolError(f"{path}: {len(image)} bytes exceed slot A ({SLOT_A_SIZE})")
    sp, reset = struct.unpack_from("<II", image, 0)
    reset_address = reset & ~1
    if not (SRAM_START <= sp <= SRAM_START + SRAM_SIZE
            and SLOT_A_START <= reset_address < SLOT_A_START + SLOT_A_SIZE):
        raise ToolError(f"{path}: vector table does not look like a slot A image")
    meta["image_crc32"] = zlib.crc32(image) & 0xFFFFFFFF
    meta["image_size"] = len(image)
    return meta


def build_frame(ftype: int, seq: int, offset: int, payload: bytes) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ToolError("payload exceeds 512 bytes")
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    header = struct.pack("<BBBBHHII", SYNC0, SYNC1, ftype, 0, seq,
                         len(payload), offset, crc)
    return header + payload


def begin_payload(meta: dict) -> bytes:
    return struct.pack("<IIIIHH", meta["image_size"], meta["image_crc32"],
                       meta["version"], meta["build_id"],
                       meta["hardware_id"], 0)


def end_payload(meta: dict) -> bytes:
    return struct.pack("<II", meta["image_size"], meta["image_crc32"])


def parse_progress_frame(frame: bytes) -> dict | None:
    if len(frame) < HEADER_SIZE:
        return None
    ftype = frame[2]
    payload_len = struct.unpack_from("<H", frame, 6)[0]
    payload = frame[HEADER_SIZE:HEADER_SIZE + payload_len]
    if ftype in (FRAME_OTA_PROGRESS, FRAME_OTA_RESULT) and len(payload) >= 16:
        state, reason, _rsv, received, total, crc = struct.unpack_from("<BBHIII", payload)
        return {"state": state, "reason": reason, "received": received,
                "total": total, "crc32": crc}
    return None


class FrameAssembler:
    """Reassemble 0xA5 0x5A frames from BLE notification fragments."""

    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, data: bytes) -> list[bytes]:
        self.buffer.extend(data)
        frames = []
        while True:
            start = self.buffer.find(bytes((SYNC0, SYNC1)))
            if start < 0:
                self.buffer.clear()
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < HEADER_SIZE:
                break
            payload_len = struct.unpack_from("<H", self.buffer, 6)[0]
            if payload_len > MAX_PAYLOAD:
                del self.buffer[:2]
                continue
            total = HEADER_SIZE + payload_len
            if len(self.buffer) < total:
                break
            frame = bytes(self.buffer[:total])
            crc = struct.unpack_from("<I", frame, 12)[0]
            payload = frame[HEADER_SIZE:]
            if crc != (zlib.crc32(payload) & 0xFFFFFFFF):
                del self.buffer[:2]
                continue
            frames.append(frame)
            del self.buffer[:total]
        return frames


class JsonAssembler:
    """Accumulate possibly fragmented JSON text and decode complete objects."""

    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, data: bytes) -> list[dict]:
        self.buffer.extend(data)
        messages = []
        while True:
            start = self.buffer.find(b"{")
            if start < 0:
                self.buffer.clear()
                break
            if start:
                del self.buffer[:start]
            depth = 0
            in_string = False
            escape = False
            end = -1
            for i, byte in enumerate(self.buffer):
                if in_string:
                    if escape:
                        escape = False
                    elif byte == 0x5C:
                        escape = True
                    elif byte == 0x22:
                        in_string = False
                    continue
                if byte == 0x22:
                    in_string = True
                elif byte == 0x7B:
                    depth += 1
                elif byte == 0x7D:
                    depth -= 1
                    if depth == 0:
                        end = i + 1
                        break
            if end < 0:
                break
            try:
                messages.append(json.loads(self.buffer[:end]))
            except json.JSONDecodeError:
                pass
            del self.buffer[:end]
        return messages


def describe_ota(msg: dict) -> str:
    state = OTA_STATE_NAMES.get(msg.get("state"), str(msg.get("state")))
    reason = REASON_NAMES.get(msg.get("reason"), str(msg.get("reason")))
    received = msg.get("received", 0)
    total = msg.get("total", 0)
    pct = 100 if total == 0 else int(received * 100 / total)
    return f"ota {state} reason={reason} {received}/{total} ({pct}%)"


# --------------------------------------------------------------------------
# Serial transport
# --------------------------------------------------------------------------

class SerialTransport:
    def __init__(self, port: str, baud: int, timeout: float) -> None:
        try:
            import serial  # pyserial
        except ImportError as exc:  # pragma: no cover - environment dependent
            raise ToolError("pyserial is required: pip install -r tools/requirements.txt") from exc
        try:
            self.serial = serial.Serial(port, baud, timeout=timeout)
        except serial.SerialException as exc:
            raise ToolError(f"cannot open {port}: {exc}") from exc
        self.json = JsonAssembler()

    def close(self) -> None:
        self.serial.close()

    def _read_some(self, timeout: float) -> bytes:
        self.serial.timeout = timeout
        return self.serial.read(1) + b"".join(
            self.serial.read(self.serial.in_waiting or 1))

    def send_json(self, payload: dict | str) -> None:
        text = payload if isinstance(payload, str) else json.dumps(payload,
                                                                  separators=(",", ":"))
        self.serial.write(text.encode("utf-8") + b"\r\n")

    def send_frame(self, frame: bytes) -> None:
        self.serial.write(frame)

    def wait_json(self, timeout: float, predicate=None, label: str = "response") -> dict:
        """Read JSON lines until @p predicate accepts one or the timeout expires."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            data = self._read_some(min(0.2, max(0.01, deadline - time.monotonic())))
            if data:
                for msg in self.json.feed(data):
                    print(json.dumps(msg, ensure_ascii=False), file=sys.stderr)
                    if predicate is None or predicate(msg):
                        return msg
        raise ToolError(f"timeout waiting for {label}")

    def request(self, payload: dict | str, timeout: float, label: str) -> dict:
        self.send_json(payload)
        return self.wait_json(timeout, label=label)


# --------------------------------------------------------------------------
# BLE transport
# --------------------------------------------------------------------------

class BleTransport:
    def __init__(self, name: str, address: str | None, scan_timeout: float) -> None:
        self.name = name
        self.address = address
        self.scan_timeout = scan_timeout
        self.client = None
        self.json = JsonAssembler()
        self.frames = FrameAssembler()
        self.json_queue: asyncio.Queue[dict] = asyncio.Queue()
        self.frame_queue: asyncio.Queue[bytes] = asyncio.Queue()

    async def connect(self) -> None:
        try:
            from bleak import BleakClient, BleakScanner
        except ImportError as exc:  # pragma: no cover - environment dependent
            raise ToolError("bleak is required for --ble: pip install -r tools/requirements.txt") from exc

        if self.address:
            self.client = BleakClient(self.address)
        else:
            devices = await BleakScanner.discover(timeout=self.scan_timeout)
            match = next((d for d in devices if (d.name or "") == self.name), None)
            if match is None:
                found = ", ".join(sorted({d.name for d in devices if d.name})[:10])
                raise ToolError(f"device {self.name!r} not found; saw: {found or 'nothing'}")
            self.client = BleakClient(match)

        await self.client.connect()
        await self.client.start_notify(CHAR_INFO, self._on_json_char)
        await self.client.start_notify(CHAR_PROVISION, self._on_json_char)
        await self.client.start_notify(CHAR_OTA_CONTROL, self._on_frame_char)

    def _on_json_char(self, _char, data: bytes) -> None:
        for msg in self.json.feed(data):
            self.json_queue.put_nowait(msg)

    def _on_frame_char(self, _char, data: bytes) -> None:
        for frame in self.frames.feed(data):
            progress = parse_progress_frame(frame)
            if progress is not None:
                self.frame_queue.put_nowait(progress)

    async def disconnect(self) -> None:
        if self.client is not None:
            await self.client.disconnect()

    async def _write(self, uuid: str, data: bytes) -> None:
        # 20-byte chunks are safe with the default ATT MTU (23) used by ESP-AT.
        for offset in range(0, len(data), 20):
            await self.client.write_gatt_char(uuid, data[offset:offset + 20], response=False)

    async def send_json(self, payload: dict | str) -> None:
        text = payload if isinstance(payload, str) else json.dumps(payload,
                                                                  separators=(",", ":"))
        await self._write(CHAR_PROVISION, text.encode("utf-8"))

    async def send_control_frame(self, frame: bytes) -> None:
        await self._write(CHAR_OTA_CONTROL, frame)

    async def send_data_frame(self, frame: bytes) -> None:
        await self._write(CHAR_OTA_DATA, frame)

    async def wait_json(self, timeout: float, predicate=None, label: str = "response") -> dict:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ToolError(f"timeout waiting for {label}")
            try:
                msg = await asyncio.wait_for(self.json_queue.get(), remaining)
            except asyncio.TimeoutError as exc:
                raise ToolError(f"timeout waiting for {label}") from exc
            print(json.dumps(msg, ensure_ascii=False), file=sys.stderr)
            if predicate is None or predicate(msg):
                return msg

    async def wait_progress(self, timeout: float, states: set[int]) -> dict:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ToolError(f"timeout waiting for OTA state {sorted(states)}")
            try:
                msg = await asyncio.wait_for(self.frame_queue.get(), remaining)
            except asyncio.TimeoutError as exc:
                raise ToolError(f"timeout waiting for OTA state {sorted(states)}") from exc
            print(describe_ota(msg), file=sys.stderr)
            if msg["state"] in states:
                return msg
            if msg["state"] == STATE_ERROR:
                raise ToolError(describe_ota(msg))


# ------------------------------------------------------------------
# OTA driver shared by both transports
# ------------------------------------------------------------------

def check_ota_message(msg: dict) -> None:
    if msg.get("type") == "ota" and msg.get("state") == STATE_ERROR:
        raise ToolError(describe_ota(msg))


def run_ota_serial(transport: SerialTransport, meta: dict, chunk: int,
                   pace: float, timeout: float) -> dict:
    seq = 0
    transport.send_frame(build_frame(FRAME_OTA_BEGIN, seq, 0, begin_payload(meta)))
    seq += 1
    start = transport.wait_json(timeout, lambda m: m.get("type") == "ota", "OTA BEGIN")
    print(describe_ota(start), file=sys.stderr)

    image = meta["image"]
    total = len(image)
    for offset in range(0, total, chunk):
        payload = image[offset:offset + chunk]
        transport.send_frame(build_frame(FRAME_OTA_DATA, seq, offset, payload))
        seq += 1
        if pace:
            time.sleep(pace)
        if (offset // chunk) % 16 == 0:
            drain_ota_serial(transport)
    drain_ota_serial(transport)

    transport.send_frame(build_frame(FRAME_OTA_END, seq, 0, end_payload(meta)))
    result = transport.wait_json(
        timeout, lambda m: m.get("type") == "ota" and m.get("state") in
        (STATE_READY_TO_BOOT, STATE_ERROR), "OTA END")
    print(describe_ota(result), file=sys.stderr)
    check_ota_message(result)
    return result


def drain_ota_serial(transport: SerialTransport) -> None:
    """Consume already-buffered progress messages without blocking."""
    while True:
        pending = transport.serial.in_waiting
        if not pending:
            return
        data = transport.serial.read(pending)
        for msg in transport.json.feed(data):
            print(json.dumps(msg, ensure_ascii=False), file=sys.stderr)
            check_ota_message(msg)


async def run_ota_ble(transport: BleTransport, meta: dict, chunk: int,
                      pace: float, timeout: float) -> dict:
    seq = 0
    await transport.send_control_frame(build_frame(FRAME_OTA_BEGIN, seq, 0,
                                                   begin_payload(meta)))
    seq += 1
    start = await transport.wait_progress(timeout, {STATE_RECEIVING})
    print(describe_ota(start), file=sys.stderr)

    image = meta["image"]
    for offset in range(0, len(image), chunk):
        await transport.send_data_frame(build_frame(FRAME_OTA_DATA, seq, offset,
                                                    image[offset:offset + chunk]))
        seq += 1
        if pace:
            await asyncio.sleep(pace)
        while not transport.frame_queue.empty():
            progress = transport.frame_queue.get_nowait()
            print(describe_ota(progress), file=sys.stderr)
            if progress["state"] == STATE_ERROR:
                raise ToolError(describe_ota(progress))

    await transport.send_control_frame(build_frame(FRAME_OTA_END, seq, 0,
                                                   end_payload(meta)))
    result = await transport.wait_progress(
        timeout, {STATE_READY_TO_BOOT, STATE_ERROR})
    print(describe_ota(result), file=sys.stderr)
    if result["state"] == STATE_ERROR:
        raise ToolError(describe_ota(result))
    return result


def wait_for_reboot_serial(transport: SerialTransport, meta: dict,
                           timeout: float) -> dict | None:
    """After the device resets, poll `info` until the new version appears."""
    expected = version_string(meta["version"]) if meta["version"] else None
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            transport.send_json("info")
            msg = transport.wait_json(2.0, lambda m: m.get("type") == "info", "info")
            if expected is None or msg.get("version") == expected:
                return msg
        except ToolError:
            pass
        time.sleep(1.0)
    return None


async def wait_for_reboot_ble(transport: BleTransport, meta: dict,
                              timeout: float, name: str, address: str | None,
                              scan_timeout: float) -> dict | None:
    expected = version_string(meta["version"]) if meta["version"] else None
    await transport.disconnect()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            fresh = BleTransport(name, address, scan_timeout)
            await fresh.connect()
            msg = await fresh.wait_json(3.0, lambda m: m.get("type") == "info",
                                        "info after reboot")
            await fresh.disconnect()
            if expected is None or msg.get("version") == expected:
                return msg
        except (ToolError, Exception):  # noqa: BLE001 - reconnect is best effort
            await asyncio.sleep(1.0)
    return None


# ------------------------------------------------------------------
# Commands
# ------------------------------------------------------------------

def wifi_command(args: argparse.Namespace) -> dict | str:
    if args.action == "status":
        return {"cmd": "wifi.status"}
    if args.action == "clear":
        return {"cmd": "wifi.clear"}
    if not args.ssid:
        raise ToolError("wifi set requires --ssid")
    return {"cmd": "wifi.set", "ssid": args.ssid,
            "password": args.password or ""}


def print_message(msg: dict) -> None:
    print(json.dumps(msg, ensure_ascii=False, indent=2))


def run_serial(args: argparse.Namespace, meta: dict | None) -> int:
    transport = SerialTransport(args.serial, args.baud, timeout=1.0)
    try:
        if args.command == "info":
            print_message(transport.request("info", 3.0, "info"))
        elif args.command == "wifi":
            cmd = wifi_command(args)
            timeout = 25.0 if (isinstance(cmd, dict) and cmd["cmd"] == "wifi.set") else 5.0
            print_message(transport.request(cmd, timeout, "wifi reply"))
        elif args.command == "ota":
            assert meta is not None
            print(json.dumps({k: (version_string(v) if k == "version" else v)
                              for k, v in meta.items() if k != "image"}, indent=2),
                  file=sys.stderr)
            run_ota_serial(transport, meta, args.chunk, args.pace, args.timeout)
            if args.verify:
                info = wait_for_reboot_serial(transport, meta, args.reboot_timeout)
                if info is None:
                    raise ToolError("device did not come back with the new version")
                print_message(info)
        elif args.command == "monitor":
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                data = transport._read_some(0.2)
                for msg in transport.json.feed(data):
                    print_message(msg)
        return 0
    finally:
        transport.close()


async def run_ble(args: argparse.Namespace, meta: dict | None) -> int:
    transport = BleTransport(args.name, args.address, args.scan_timeout)
    await transport.connect()
    try:
        if args.command == "info":
            print_message(await transport.wait_json(5.0,
                                                    lambda m: m.get("type") == "info",
                                                    "info notification"))
        elif args.command == "wifi":
            cmd = wifi_command(args)
            await transport.send_json(cmd)
            timeout = 25.0 if (isinstance(cmd, dict) and cmd["cmd"] == "wifi.set") else 5.0
            print_message(await transport.wait_json(
                timeout, lambda m: m.get("type") == "wifi", "wifi reply"))
        elif args.command == "ota":
            assert meta is not None
            print(json.dumps({k: (version_string(v) if k == "version" else v)
                              for k, v in meta.items() if k != "image"}, indent=2),
                  file=sys.stderr)
            await run_ota_ble(transport, meta, args.chunk, args.pace, args.timeout)
            if args.verify:
                info = await wait_for_reboot_ble(transport, meta, args.reboot_timeout,
                                                 args.name, args.address,
                                                 args.scan_timeout)
                if info is None:
                    raise ToolError("device did not come back with the new version")
                print_message(info)
        elif args.command == "monitor":
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                try:
                    print_message(await asyncio.wait_for(transport.json_queue.get(), 0.5))
                except asyncio.TimeoutError:
                    pass
                while not transport.frame_queue.empty():
                    print_message(await transport.frame_queue.get())
        return 0
    finally:
        await transport.disconnect()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    link = parser.add_mutually_exclusive_group(required=True)
    link.add_argument("--serial", metavar="PORT")
    link.add_argument("--ble", action="store_true")
    parser.add_argument("--name", default="STM-BLE-OTA")
    parser.add_argument("--address", default=None,
                        help="BLE address; skips scanning")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--scan-timeout", type=float, default=10.0)
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="overall OTA / monitor timeout")
    parser.add_argument("--reboot-timeout", type=float, default=20.0)
    parser.add_argument("--chunk", type=int, default=512,
                        help="OTA data payload size (1..512)")
    parser.add_argument("--pace", type=float, default=0.0,
                        help="delay between DATA frames in seconds")
    parser.add_argument("--verify", action="store_true",
                        help="after OTA, wait for reboot and read the new version")

    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("info", help="read device information")
    monitor = sub.add_parser("monitor", help="print incoming messages until --timeout")
    monitor.set_defaults(command="monitor")

    wifi = sub.add_parser("wifi", help="provisioning commands")
    wifi.add_argument("action", choices=("status", "set", "clear"))
    wifi.add_argument("--ssid")
    wifi.add_argument("--password")

    ota = sub.add_parser("ota", help="flash a .ota or .bin image")
    ota.add_argument("image", type=Path)
    # Repeat the transfer options on the subcommand so they may also follow
    # `ota <image>`; SUPPRESS keeps the top-level value when not given here.
    ota.add_argument("--timeout", type=float, default=argparse.SUPPRESS)
    ota.add_argument("--reboot-timeout", type=float, default=argparse.SUPPRESS)
    ota.add_argument("--chunk", type=int, default=argparse.SUPPRESS)
    ota.add_argument("--pace", type=float, default=argparse.SUPPRESS)
    ota.add_argument("--verify", action="store_true", default=argparse.SUPPRESS)

    args = parser.parse_args()
    if not 1 <= args.chunk <= MAX_PAYLOAD:
        parser.error("--chunk must be in 1..512")

    meta = None
    if args.command == "ota":
        try:
            meta = load_image(args.image)
        except ToolError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 2

    try:
        if args.serial:
            return run_serial(args, meta)
        return asyncio.run(run_ble(args, meta))
    except ToolError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
