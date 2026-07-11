#!/usr/bin/env python3
"""Compile advertised wireless features from an exact release archive."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path


FEATURE_BUILDS = (
    (
        "xiao_nrf54l15",
        "libraries/Bluefruit52Lib/examples/Security/pairing_numeric_comparison",
    ),
    (
        "xiao_nrf54l15",
        "libraries/Nrf54L15-Clean-Implementation/examples/BLE/Security/BleOobPairPeripheral",
    ),
    (
        "xiao_nrf54lm20b",
        "libraries/Nrf54L15-Clean-Implementation/examples/BLE/Security/BleOobPairCentral",
    ),
    (
        "xiao_nrf54l15",
        "libraries/Nrf54L15-Clean-Implementation/examples/BLE/Privacy/BleResolvablePrivateAddress",
    ),
    (
        "xiao_nrf54lm20b",
        "libraries/Nrf54L15-Clean-Implementation/examples/BLE/Scanning/BleActiveScanner",
    ),
    (
        "xiao_nrf54l15:clean_ble=on,cpu_freq=128m",
        "libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingInitiator",
    ),
    (
        "xiao_nrf54lm20b:clean_ble=on,cpu_freq=128m",
        "libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingReflector",
    ),
    (
        "xiao_nrf54l15:clean_thread=stage",
        "libraries/Nrf54L15-Clean-Implementation/examples/Thread/OpenThreadCoreStageProbe",
    ),
    (
        "xiao_nrf54lm20b:clean_thread=stage",
        "libraries/Nrf54L15-Clean-Implementation/examples/Thread/OpenThreadCoreStageProbe",
    ),
    (
        "xiao_nrf54l15:clean_thread=stage,clean_matter=stage",
        "libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnOffLightFoundationCompileTarget",
    ),
    (
        "xiao_nrf54lm20b:clean_thread=stage,clean_matter=stage",
        "libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnOffLightFoundationCompileTarget",
    ),
)
LOCAL_PACKAGER = "localnrf54"
DECLARED_COMPILER_VERSION = "7-2017q4"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--compiler-path", type=Path)
    parser.add_argument("--timeout", type=int, default=900)
    return parser.parse_args()


def find_compiler_path() -> Path | None:
    roots = (
        Path.home() / ".arduino15" / "packages" / "arduino" / "tools" / "arm-none-eabi-gcc",
        Path.home() / "Library" / "Arduino15" / "packages" / "arduino" / "tools" / "arm-none-eabi-gcc",
        Path(os.environ.get("LOCALAPPDATA", "")) / "Arduino15" / "packages" / "arduino" / "tools" / "arm-none-eabi-gcc",
    )
    for root in roots:
        declared = root / DECLARED_COMPILER_VERSION / "bin"
        if declared.is_dir():
            return declared
    return None


def main() -> int:
    args = parse_args()
    archive = args.archive.resolve()
    if not archive.is_file():
        raise SystemExit(f"archive not found: {archive}")
    if shutil.which("arduino-cli") is None:
        raise SystemExit("arduino-cli is required")
    compiler = args.compiler_path.resolve() if args.compiler_path else find_compiler_path()
    if compiler is None or not compiler.is_dir():
        raise SystemExit(
            f"ARM compiler {DECLARED_COMPILER_VERSION} bin directory not found; "
            "pass --compiler-path"
        )

    with tempfile.TemporaryDirectory(prefix="nrf54-release-verify-") as td:
        temp = Path(td)
        extracted = temp / "extracted"
        extracted.mkdir()
        with tarfile.open(archive, "r:*") as tar:
            tar.extractall(extracted)
        roots = [path for path in extracted.iterdir() if path.is_dir()]
        if len(roots) != 1:
            raise SystemExit(f"archive must contain one root directory, found {len(roots)}")
        platform = roots[0]

        required = (
            platform / "libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core",
            platform / "libraries/Nrf54L15-Clean-Implementation/src/openthread_core_stage",
            platform / "libraries/Nrf54L15-Clean-Implementation/src/openthread_core_stage_bridge.cpp",
        )
        missing = [str(path.relative_to(platform)) for path in required if not path.exists()]
        if missing:
            raise SystemExit(f"release archive omits advertised feature sources: {missing}")

        user = temp / "user"
        data = Path(
            os.environ.get("ARDUINO_DATA_DIR", str(Path.home() / ".arduino15"))
        ).expanduser().resolve()
        if not data.is_dir():
            raise SystemExit(f"Arduino data directory does not exist: {data}")
        downloads = temp / "downloads"
        # Keep the extracted archive in a distinct namespace so an installed
        # Board Manager core cannot satisfy these compiles by accident.
        hardware = user / "hardware" / LOCAL_PACKAGER
        hardware.mkdir(parents=True)
        downloads.mkdir()
        (hardware / "nrf54l15clean").symlink_to(platform, target_is_directory=True)
        config = temp / "arduino-cli.yaml"
        config.write_text(
            "directories:\n"
            f"  data: {data}\n"
            f"  downloads: {downloads}\n"
            f"  user: {user}\n",
            encoding="utf-8",
        )

        for index, (board_options, sketch) in enumerate(FEATURE_BUILDS):
            fqbn = f"{LOCAL_PACKAGER}:nrf54l15clean:{board_options}"
            cmd = [
                "arduino-cli",
                "compile",
                "--config-file",
                str(config),
                "--fqbn",
                fqbn,
                "--build-property",
                f"compiler.path={compiler}{os.sep}",
                "--build-path",
                str(temp / f"build-{index}"),
                str(platform / sketch),
            ]
            completed = subprocess.run(cmd, capture_output=True, text=True, timeout=args.timeout)
            if completed.returncode != 0:
                output = completed.stdout + completed.stderr
                raise SystemExit(f"archive feature compile failed for {fqbn}:\n{output[-12000:]}")
            print(f"archive feature compile OK: {fqbn} {sketch}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
