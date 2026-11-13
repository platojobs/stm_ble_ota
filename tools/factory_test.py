#!/usr/bin/env python3
"""One-shot production test over the USART2 test link.

Optionally programs the unit serial, runs `factory test`, and writes the JSON
result (including the host timestamp and port) to a file. Exit code 0 means the
unit passed; 1 means a check failed; 2 means the tool or link failed.

Example:
    python3 tools/factory_test.py --port /dev/tty.usbmodem1101 \
        --serial C562-000123 --out build/factory/C562-000123.json
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable


def read_json_line(port, deadline: float, want: Callable[[dict], bool]) -> dict:
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", "replace").strip()
        if not line.startswith("{"):
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        if want(obj):
            return obj
    raise TimeoutError("no matching JSON response before timeout")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="serial port of the fixture")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--serial", help="program this serial before the test")
    parser.add_argument("--out", default="build/factory_result.json",
                        help="JSON result path (default: build/factory_result.json)")
    parser.add_argument("--timeout", type=float, default=8.0,
                        help="seconds to wait for each response (default 8)")
    args = parser.parse_args()

    try:
        import serial  # type: ignore
    except ImportError:
        print("pyserial is required: python3 -m pip install pyserial", file=sys.stderr)
        return 2

    record = {
        "type": "factory_record",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "port": args.port,
        "serial_write": None,
        "result": None,
        "passed": False,
    }

    try:
        with serial.Serial(args.port, args.baud, timeout=0.5) as port:
            port.reset_input_buffer()

            if args.serial:
                port.write(f"serial set {args.serial}\r\n".encode("ascii"))
                port.flush()
                ack = read_json_line(
                    port, time.monotonic() + args.timeout,
                    lambda obj: obj.get("cmd") == "serial")
                record["serial_write"] = ack
                if not ack.get("ok"):
                    record["error"] = ack.get("error", "serial write failed")
                else:
                    print(f"serial programmed: {ack.get('serial')}")

            if record["serial_write"] is None or record["serial_write"].get("ok"):
                port.write(b"factory test\r\n")
                port.flush()
                result = read_json_line(
                    port, time.monotonic() + args.timeout,
                    lambda obj: obj.get("cmd") == "factory" and
                    obj.get("action") == "test")
                record["result"] = result
                record["passed"] = bool(result.get("ok"))
    except (OSError, TimeoutError) as exc:
        record["error"] = str(exc)
        print(f"factory test failed: {exc}", file=sys.stderr)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")

    result = record["result"] or {}
    if record["passed"]:
        print(f"PASS {result.get('serial', '')} "
              f"fw {result.get('firmware', '?')} -> {out}")
        return 0
    if result:
        print(f"FAIL failed_mask=0x{int(result.get('failed_mask', 0)):02X} "
              f"serial={result.get('serial', '')} -> {out}", file=sys.stderr)
    else:
        print(f"FAIL no result (see {out})", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
