#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import List

from zephyr_common import core_paths, sdk_tool


ALIASES = {
    "arm-none-eabi-gcc": "arm-zephyr-eabi-gcc",
    "arm-none-eabi-g++": "arm-zephyr-eabi-g++",
    "arm-none-eabi-ar": "arm-zephyr-eabi-ar",
    "arm-none-eabi-objcopy": "arm-zephyr-eabi-objcopy",
    "arm-none-eabi-size": "arm-zephyr-eabi-size",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Resolve Zephyr SDK tool and execute it.")
    parser.add_argument("--tool", required=True, help="Tool basename (for example: arm-zephyr-eabi-gcc)")
    parser.add_argument("tool_args", nargs=argparse.REMAINDER, help="Arguments forwarded to the tool")
    return parser.parse_args()


def normalize_tool_name(raw_tool: str) -> str:
    name = Path(raw_tool).name
    lowered = name.lower()
    for suffix in (".exe", ".cmd", ".bat"):
        if lowered.endswith(suffix):
            name = name[: -len(suffix)]
            lowered = lowered[: -len(suffix)]
            break
    return ALIASES.get(lowered, name)


def resolve_tool_path(tool_name: str) -> Path:
    paths = core_paths(Path(__file__))
    script_dir = paths["script_dir"]
    sdk_dir = paths["sdk_dir"]

    resolved = sdk_tool(sdk_dir, tool_name)
    if resolved and resolved.is_file():
        return resolved

    # First-run compile on clean machines may reach compiler invocation before
    # Zephyr SDK bootstrap has completed; provision it lazily here.
    subprocess.run([sys.executable, str(script_dir / "get_toolchain.py")], check=True)

    resolved = sdk_tool(sdk_dir, tool_name)
    if resolved and resolved.is_file():
        return resolved

    raise RuntimeError(f"Unable to locate Zephyr SDK tool '{tool_name}' under {sdk_dir}")


def normalized_tool_args(raw_args: List[str]) -> List[str]:
    if raw_args and raw_args[0] == "--":
        return raw_args[1:]
    return raw_args


def main() -> int:
    args = parse_args()
    tool_name = normalize_tool_name(args.tool)
    tool_path = resolve_tool_path(tool_name)
    forwarded = normalized_tool_args(args.tool_args)
    result = subprocess.run([str(tool_path), *forwarded], check=False)
    return result.returncode


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover - passthrough helper
        print(str(exc), file=sys.stderr)
        raise
