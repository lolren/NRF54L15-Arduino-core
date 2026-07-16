#!/usr/bin/env python3
"""Remove Arduino objects compiled for the other nRF54 SoC core."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


TARGETS = ("nrf54l15", "nrf54lm20b")
MARKER_NAME = ".nrf54-clean-build-target"


def dependency_mentions(path: Path, target: str) -> bool:
    try:
        content = path.read_bytes()
    except OSError:
        return False
    unix_token = f"/cores/{target}/".encode()
    windows_token = f"\\cores\\{target}\\".encode()
    return unix_token in content or windows_token in content


def stale_reason(build_path: Path, target: str, platform_path: Path) -> str | None:
    marker = build_path / MARKER_NAME
    try:
        marker_lines = marker.read_text(encoding="utf-8").splitlines()
    except OSError:
        marker_lines = []
    previous = marker_lines[0].strip() if marker_lines else ""
    previous_platform = marker_lines[1].strip() if len(marker_lines) > 1 else ""
    if previous in TARGETS and previous != target:
        return f"target changed from {previous} to {target}"
    if previous and previous_platform != str(platform_path):
        return "platform installation changed"
    if not previous and any(build_path.rglob("*.d")):
        return "existing cache predates target tracking"

    for candidate in TARGETS:
        if candidate == target:
            continue
        if any(dependency_mentions(path, candidate) for path in build_path.rglob("*.d")):
            return f"dependencies contain {candidate} objects"
    return None


def clean_build_cache(
    build_path: Path, target: str, platform_path: Path | None = None
) -> str | None:
    if target not in TARGETS:
        raise ValueError(f"unsupported nRF54 build target: {target}")

    build_path.mkdir(parents=True, exist_ok=True)
    platform_path = (platform_path or Path(__file__).resolve().parents[1]).resolve()
    reason = stale_reason(build_path, target, platform_path)
    if reason is not None:
        for directory in (build_path / "core", build_path / "libraries"):
            shutil.rmtree(directory, ignore_errors=True)

        sketch = build_path / "sketch"
        if sketch.is_dir():
            for pattern in ("*.o", "*.d", "*.a"):
                for path in sketch.rglob(pattern):
                    path.unlink(missing_ok=True)

        for pattern in (
            "*.elf",
            "*.map",
            "*.hex",
            "*.bin",
            "*.uf2",
            "*.a",
            "includes.cache",
            "libraries.cache",
            "compile_commands.json",
        ):
            for path in build_path.glob(pattern):
                path.unlink(missing_ok=True)

    (build_path / MARKER_NAME).write_text(
        f"{target}\n{platform_path}\n", encoding="utf-8"
    )
    return reason


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-path", required=True, type=Path)
    parser.add_argument("--target", required=True, choices=TARGETS)
    parser.add_argument("--platform-path", required=True, type=Path)
    args = parser.parse_args()

    reason = clean_build_cache(args.build_path, args.target, args.platform_path)
    if reason is not None:
        print(f"nRF54 cleared stale Arduino objects: {reason}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
