#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def parse_size_output(text: str) -> tuple[int, int] | None:
    # GNU size Berkeley-style output:
    # text data bss dec hex filename
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.lower().startswith("text"):
            continue
        parts = line.split()
        if len(parts) < 6:
            continue
        try:
            text_size = int(parts[0], 10)
            data_size = int(parts[1], 10)
            bss_size = int(parts[2], 10)
        except ValueError:
            continue

        flash_used = text_size + data_size
        ram_used = data_size + bss_size
        return flash_used, ram_used

    return None


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: report_size.py <size_cmd> <elf>", file=sys.stderr)
        print("PROGRAM:0")
        print("DATA:0")
        return 0

    size_cmd = Path(sys.argv[1])
    elf = Path(sys.argv[2])
    if not elf.is_file():
        print(f"Missing ELF for size reporting: {elf}", file=sys.stderr)
        print("PROGRAM:0")
        print("DATA:0")
        return 0

    try:
        result = subprocess.run(
            [str(size_cmd), str(elf)],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as exc:
        print(f"Size command launch failed: {exc}", file=sys.stderr)
        print("PROGRAM:0")
        print("DATA:0")
        return 0

    if result.returncode != 0:
        if result.stderr:
            print(result.stderr.strip(), file=sys.stderr)
        print("PROGRAM:0")
        print("DATA:0")
        return 0

    parsed = parse_size_output(result.stdout)
    if not parsed:
        print("PROGRAM:0")
        print("DATA:0")
        return 0

    flash_used, ram_used = parsed
    print(f"PROGRAM:{flash_used}")
    print(f"DATA:{ram_used}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
