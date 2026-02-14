#!/usr/bin/env python3
from __future__ import annotations

import shlex
import shutil
import sys
import tempfile
from pathlib import Path
from typing import List

from zephyr_common import core_paths, read_metadata, resolve_program, run, sdk_tool, with_west_pythonpath


def parse_object_files(raw_args: List[str]) -> List[str]:
    out: List[str] = []
    for arg in raw_args:
        cleaned = arg.replace('"', "").strip()
        if not cleaned:
            continue
        out.extend([token for token in shlex.split(cleaned) if token])
    return out


def resolve_archive_tool(sdk_dir: Path, tool: str, fallback_names: List[str]) -> str:
    from_sdk = sdk_tool(sdk_dir, tool)
    if from_sdk and from_sdk.is_file():
        return str(from_sdk)

    resolved = resolve_program(fallback_names)
    if resolved:
        return resolved

    raise RuntimeError(f"Unable to find required tool '{tool}'.")


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

    platform_dir = (variant_dir / ".." / "..").resolve()
    paths = core_paths(Path(__file__))
    sdk_dir = paths["sdk_dir"]
    ncs_dir = paths["ncs_dir"]

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

    if not archive_file.is_file():
        raise RuntimeError(f"Missing core archive: {archive_file}")

    base_app_archive = zephyr_lib_dir / "app_base.a"
    app_archive = zephyr_build_dir / "app" / "libapp.a"
    if not base_app_archive.is_file():
        if app_archive.is_file():
            shutil.copy2(app_archive, base_app_archive)
        else:
            raise RuntimeError(f"Missing base Zephyr app archive: {base_app_archive}")

    ar_bin = resolve_archive_tool(sdk_dir, "arm-zephyr-eabi-ar", ["arm-zephyr-eabi-ar", "ar"])
    ranlib_bin = resolve_archive_tool(sdk_dir, "arm-zephyr-eabi-ranlib", ["arm-zephyr-eabi-ranlib", "ranlib"])
    ninja_bin = resolve_program(["ninja", "ninja-build"])
    sdk_ninja = sdk_tool(sdk_dir, "ninja")
    if sdk_ninja and sdk_ninja.is_file():
        ninja_bin = str(sdk_ninja)
    if not ninja_bin:
        raise RuntimeError("Unable to find ninja executable.")

    shutil.copy2(base_app_archive, app_archive)

    if object_files:
        run([ar_bin, "q", str(app_archive), *object_files], check=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        run([ar_bin, "x", str(archive_file)], cwd=tmp_path, check=True)
        core_objects = sorted([str(p) for p in tmp_path.glob("*.o")])
        if core_objects:
            run([ar_bin, "q", str(app_archive), *core_objects], check=True)

    run([ranlib_bin, str(app_archive)], check=True)

    env = with_west_pythonpath(platform_dir)
    run([ninja_bin, "-C", str(zephyr_build_dir), "zephyr/zephyr.elf"], cwd=ncs_dir, env=env, check=True)

    zephyr_out = zephyr_build_dir / "zephyr"
    arduino_build_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(zephyr_out / "zephyr.elf", arduino_build_dir / f"{project_name}.elf")
    shutil.copy2(zephyr_out / "zephyr.hex", arduino_build_dir / f"{project_name}.hex")
    shutil.copy2(zephyr_out / "zephyr.bin", arduino_build_dir / f"{project_name}.bin")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

