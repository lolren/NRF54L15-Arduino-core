#!/usr/bin/env python3
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List

from zephyr_common import (
    core_paths,
    prepend_sdk_tool_paths,
    read_metadata,
    resolve_program,
    run,
    sdk_tool,
    west_cmd,
    with_west_pythonpath,
)


def parse_supported_runners(runners_file: Path) -> List[str]:
    if not runners_file.is_file():
        return ["openocd", "jlink", "nrfutil", "nrfjprog"]

    runners: List[str] = []
    in_section = False
    for raw in runners_file.read_text(encoding="utf-8").splitlines():
        line = raw.rstrip()
        if line.strip() == "runners:":
            in_section = True
            continue
        if in_section and not line.strip():
            break
        if in_section and line.lstrip().startswith("- "):
            runners.append(line.split("- ", 1)[1].strip())

    return runners if runners else ["openocd", "jlink", "nrfutil", "nrfjprog"]


def has_nrfjprog_probe() -> bool:
    if not resolve_program(["nrfjprog", "nrfjprog.exe"]):
        return False
    try:
        out = subprocess.check_output(["nrfjprog", "--ids"], text=True, stderr=subprocess.DEVNULL)
        return bool(out.strip())
    except Exception:
        return False


def runner_available(runner: str, sdk_dir: Path) -> bool:
    if runner == "nrfjprog":
        return resolve_program(["nrfjprog", "nrfjprog.exe"]) is not None
    if runner == "openocd":
        if resolve_program(["openocd", "openocd.exe"]):
            return True
        return sdk_tool(sdk_dir, "openocd") is not None
    if runner == "jlink":
        return (
            resolve_program(["JLinkExe", "JLink.exe", "JLinkGDBServer", "JLinkGDBServer.exe"]) is not None
        )
    if runner == "nrfutil":
        binary = resolve_program(["nrfutil", "nrfutil.exe"])
        if not binary:
            return False
        try:
            subprocess.check_call([binary, "--help"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return True
        except Exception:
            return False
    return False


def default_runner(supported: List[str], sdk_dir: Path) -> str:
    if "nrfjprog" in supported and runner_available("nrfjprog", sdk_dir) and has_nrfjprog_probe():
        return "nrfjprog"
    for candidate in ["openocd", "jlink", "nrfutil"]:
        if candidate in supported and runner_available(candidate, sdk_dir):
            return candidate
    return supported[0] if supported else "openocd"


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: flash_zephyr.py <variant_dir> [runner]", file=sys.stderr)
        return 1

    variant_dir = Path(sys.argv[1]).resolve()
    runner_input = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("ARDUINO_ZEPHYR_RUNNER", "auto")
    platform_dir = (variant_dir / ".." / "..").resolve()
    paths = core_paths(Path(__file__))
    sdk_dir = paths["sdk_dir"]
    ncs_dir = paths["ncs_dir"]

    zephyr_lib_dir = variant_dir / "zephyr_lib"
    metadata_file = zephyr_lib_dir / "metadata.env"
    metadata = read_metadata(metadata_file)
    build_dir = Path(metadata.get("ARDUINO_ZEPHYR_BUILD_DIR", str(zephyr_lib_dir / "build")))
    runners_file = build_dir / "zephyr" / "runners.yaml"

    if not build_dir.is_dir():
        run([sys.executable, str(platform_dir / "tools" / "build_zephyr_lib.py")], check=True)
        metadata = read_metadata(metadata_file)
        build_dir = Path(metadata.get("ARDUINO_ZEPHYR_BUILD_DIR", str(zephyr_lib_dir / "build")))
        runners_file = build_dir / "zephyr" / "runners.yaml"

    if not build_dir.is_dir():
        raise RuntimeError(f"Missing Zephyr build directory: {build_dir}")

    supported_runners = parse_supported_runners(runners_file)
    runner = runner_input
    if runner_input == "auto":
        runner = default_runner(supported_runners, sdk_dir)
        print(f"Auto-selected upload runner: {runner}")

    if runner not in supported_runners:
        raise RuntimeError(
            f"Runner '{runner}' is not supported by this build. Supported runners: {' '.join(supported_runners)}"
        )

    env = with_west_pythonpath(platform_dir)
    env["ZEPHYR_SDK_INSTALL_DIR"] = str(sdk_dir)
    prepend_sdk_tool_paths(env, sdk_dir)

    # Ensure west import and command path are valid before flash.
    run(west_cmd() + ["--version"], env=env, check=True, capture_output=True)

    extra_args: List[str] = []
    dev_id = os.environ.get("ARDUINO_ZEPHYR_DEV_ID", "")
    if dev_id and runner in ("nrfjprog", "nrfutil", "jlink"):
        extra_args += ["--dev-id", dev_id]

    flash_cmd = west_cmd() + ["flash", "-d", str(build_dir), "-r", runner, *extra_args]
    if os.environ.get("ARDUINO_ZEPHYR_FLASH_DRY_RUN", "0") == "1":
        print("Dry run command: " + " ".join([shlex_quote(x) for x in flash_cmd]))
        return 0

    run(flash_cmd, cwd=ncs_dir, env=env, check=True)
    return 0


def shlex_quote(s: str) -> str:
    if not s:
        return "''"
    if all(ch.isalnum() or ch in "._/-" for ch in s):
        return s
    return "'" + s.replace("'", "'\"'\"'") + "'"


if __name__ == "__main__":
    raise SystemExit(main())
