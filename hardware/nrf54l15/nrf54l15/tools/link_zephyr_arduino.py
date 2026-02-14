#!/usr/bin/env python3
from __future__ import annotations

import shutil
import sys
import tempfile
import re
from pathlib import Path
from typing import List

from zephyr_common import core_paths, read_metadata, resolve_program, run, sdk_tool, with_west_pythonpath

EXCLUDED_CORE_OBJECTS = {
    "core_install.c.o",
    "startup_nrf54l15.S.o",
    "syscalls.c.o",
    "system_nrf54l15.c.o",
}


def parse_object_files(raw_args: List[str]) -> List[str]:
    def split_object_like(text: str) -> List[str]:
        matches = re.findall(r'(?:[A-Za-z]:)?[^"\r\n]*?\.o(?:bj)?', text)
        return [match.strip() for match in matches if match.strip()]

    out: List[str] = []
    for arg in raw_args:
        cleaned = arg.strip()
        if not cleaned:
            continue

        # Arduino often forwards object lists as a single argument with escaped quotes.
        normalized = cleaned.replace('\\"', '"')
        quoted_segments = re.findall(r'"([^"]+)"', normalized)
        if quoted_segments:
            for segment in quoted_segments:
                split_paths = split_object_like(segment)
                if split_paths:
                    out.extend(split_paths)
                else:
                    out.append(segment.strip())
            continue

        normalized = normalized.replace('"', "").replace("'", "")
        split_paths = split_object_like(normalized)
        if split_paths:
            out.extend(split_paths)
        else:
            out.extend(token for token in normalized.split() if token)
    return out


def resolve_archive_tool(sdk_dir: Path, tool: str, fallback_names: List[str]) -> str:
    from_sdk = sdk_tool(sdk_dir, tool)
    if from_sdk and from_sdk.is_file():
        return str(from_sdk)

    resolved = resolve_program(fallback_names)
    if resolved:
        return resolved

    raise RuntimeError(f"Unable to find required tool '{tool}'.")


def resolve_ninja(sdk_dir: Path) -> str:
    from_sdk = sdk_tool(sdk_dir, "ninja")
    if from_sdk and from_sdk.is_file():
        return str(from_sdk)

    resolved = resolve_program(["ninja", "ninja-build"])
    if resolved:
        return resolved

    raise RuntimeError("Unable to find ninja executable.")


def main() -> int:
    if len(sys.argv) < 6:
        print(
            "Usage: link_zephyr_arduino.py <variant_dir> <arduino_build_dir> <project_name> <object_files> <archive_file>",
            file=sys.stderr,
        )
        return 1

    variant_dir = Path(sys.argv[1]).resolve()
    arduino_build_dir = Path(sys.argv[2]).resolve()
    project_name = sys.argv[3]
    archive_file = Path(sys.argv[-1]).resolve()
    object_files_raw = sys.argv[4:-1]
    object_files = parse_object_files(object_files_raw)

    if not archive_file.is_file():
        raise RuntimeError(f"Missing core archive: {archive_file}")

    platform_dir = (variant_dir / ".." / "..").resolve()
    paths = core_paths(Path(__file__))
    sdk_dir = paths["sdk_dir"]
    ncs_dir = paths["ncs_dir"]
    tooling_platform_dir = paths["tooling_platform_dir"]

    zephyr_lib_dir = variant_dir / "zephyr_lib"
    metadata_file = zephyr_lib_dir / "metadata.env"
    metadata = read_metadata(metadata_file)
    zephyr_build_dir = Path(metadata.get("ARDUINO_ZEPHYR_BUILD_DIR", str(zephyr_lib_dir / "build")))

    if not zephyr_build_dir.is_dir():
        run([sys.executable, str(platform_dir / "tools" / "build_zephyr_lib.py")], check=True)
        metadata = read_metadata(metadata_file)
        zephyr_build_dir = Path(metadata.get("ARDUINO_ZEPHYR_BUILD_DIR", str(zephyr_lib_dir / "build")))

    if not zephyr_build_dir.is_dir():
        raise RuntimeError(f"Missing Zephyr build directory: {zephyr_build_dir}")

    app_archive = zephyr_build_dir / "app" / "libapp.a"
    app_base_archive = zephyr_lib_dir / "app_base.a"
    if not app_base_archive.is_file():
        raise RuntimeError(f"Missing base Zephyr app archive: {app_base_archive}")
    if not app_archive.parent.is_dir():
        raise RuntimeError(f"Missing Zephyr app output directory: {app_archive.parent}")

    ar_bin = resolve_archive_tool(sdk_dir, "arm-zephyr-eabi-ar", ["arm-zephyr-eabi-ar", "ar"])
    ranlib_bin = resolve_archive_tool(sdk_dir, "arm-zephyr-eabi-ranlib", ["arm-zephyr-eabi-ranlib", "ranlib"])
    ninja_bin = resolve_ninja(sdk_dir)

    shutil.copy2(app_base_archive, app_archive)

    if object_files:
        run([ar_bin, "q", str(app_archive), *object_files], check=True)

    with tempfile.TemporaryDirectory() as tempdir:
        temp_path = Path(tempdir)
        run([ar_bin, "x", str(archive_file)], cwd=temp_path, check=True)
        core_objects = sorted(
            str(path)
            for path in temp_path.glob("*.o")
            if path.name not in EXCLUDED_CORE_OBJECTS
        )
        if core_objects:
            run([ar_bin, "q", str(app_archive), *core_objects], check=True)

    run([ranlib_bin, str(app_archive)], check=True)

    env = with_west_pythonpath(tooling_platform_dir)
    run([ninja_bin, "-C", str(zephyr_build_dir), "zephyr/zephyr.elf"], cwd=ncs_dir, env=env, check=True)

    zephyr_out = zephyr_build_dir / "zephyr"
    arduino_build_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(zephyr_out / "zephyr.elf", arduino_build_dir / f"{project_name}.elf")
    shutil.copy2(zephyr_out / "zephyr.hex", arduino_build_dir / f"{project_name}.hex")
    shutil.copy2(zephyr_out / "zephyr.bin", arduino_build_dir / f"{project_name}.bin")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
