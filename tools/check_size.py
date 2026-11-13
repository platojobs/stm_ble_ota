#!/usr/bin/env python3
"""Check Flash/RAM usage of the firmware images against the product budgets.

Reads the GNU ld map files produced by the build, sums the output sections that
end up in Flash and in RAM, and exits non-zero when a budget is exceeded.

Budgets (also documented in docs/toolchain.md):
  bootloader  Flash 30 KB   RAM 8 KB
  app         Flash 92 KB   RAM 24 KB   (the app slot is 96 KB)

Usage: python3 tools/check_size.py [--build-dir build/arm]
"""

import argparse
import re
import sys
from pathlib import Path

FLASH_SECTIONS = {
    ".vectors", ".text", ".iplt", ".rodata", ".ARM.extab", ".ARM",
    ".preinit_array", ".init_array", ".fini_array", ".copy.table",
    ".zero.table", ".rel.dyn", ".data",
}
RAM_SECTIONS = {
    ".data", ".igot.plt", ".bss", ".noinit", ".heap", ".stack",
}

BUDGETS = {
    "bootloader": {"flash": 30 * 1024, "ram": 8 * 1024},
    "app": {"flash": 92 * 1024, "ram": 24 * 1024},
}

MAP_FILES = {
    "bootloader": "bootloader.map",
    "app": "stm_ble_ota_app.map",
}

SECTION_RE = re.compile(r"^(\.[A-Za-z0-9_.]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)")


def parse_map(path):
    """Return {output_section: size} for a GNU ld map file."""
    sections = {}
    for line in path.read_text(errors="replace").splitlines():
        if not line or line[0] != ".":
            continue
        match = SECTION_RE.match(line)
        if not match:
            continue
        name, _address, size = match.groups()
        sections[name] = int(size, 16)
    return sections


def usage(sections):
    flash = sum(size for name, size in sections.items() if name in FLASH_SECTIONS)
    ram = sum(size for name, size in sections.items() if name in RAM_SECTIONS)
    return flash, ram


def check(build_dir):
    failed = False
    print(f"{'image':<12}{'flash':>10}{'flash budget':>15}"
          f"{'ram':>10}{'ram budget':>15}  result")
    for image, filename in MAP_FILES.items():
        path = build_dir / filename
        if not path.is_file():
            print(f"{image:<12}missing map: {path}", file=sys.stderr)
            failed = True
            continue

        flash, ram = usage(parse_map(path))
        budget = BUDGETS[image]
        ok = flash <= budget["flash"] and ram <= budget["ram"]
        failed = failed or not ok
        print(f"{image:<12}{flash:>10}{budget['flash']:>15}"
              f"{ram:>10}{budget['ram']:>15}  {'ok' if ok else 'OVER BUDGET'}")

        if flash > budget["flash"]:
            print(f"  {image}: flash {flash} B exceeds {budget['flash']} B",
                  file=sys.stderr)
        if ram > budget["ram"]:
            print(f"  {image}: RAM {ram} B exceeds {budget['ram']} B",
                  file=sys.stderr)

    return 0 if not failed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build/arm",
                        help="directory containing the .map files")
    args = parser.parse_args()
    return check(Path(args.build_dir))


if __name__ == "__main__":
    sys.exit(main())
