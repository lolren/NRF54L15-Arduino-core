#!/usr/bin/env python3
"""Build and run the host-side Matter Arduino System Layer regression test."""

from __future__ import annotations

import os
import pathlib
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
IMPL = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries"
    / "Nrf54L15-Clean-Implementation"
)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="matter-system-layer-") as directory:
        common = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-no-pie",
            "-no-pie",
        ]
        output = pathlib.Path(directory) / "matter_system_layer_test"
        command = common + [
            f"-I{ROOT / 'tests/matter_platform_stubs'}",
            f"-I{IMPL / 'src/matter_core_stage'}",
            f"-I{IMPL / 'src/platform/arduino'}",
            f"-I{IMPL / 'third_party/connectedhomeip/src'}",
            str(ROOT / "tests/core_io/matter_system_layer_test.cpp"),
            "-o",
            str(output),
        ]
        subprocess.run(command, check=True, cwd=ROOT)
        sanitizer_env = os.environ.copy()
        sanitizer_env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
        subprocess.run(
            [str(output)], check=True, cwd=ROOT, env=sanitizer_env
        )

        inet_output = pathlib.Path(directory) / "matter_inet_endpoint_test"
        inet_command = common + [
            f"-I{ROOT / 'tests/matter_inet_stubs'}",
            f"-I{ROOT / 'tests/matter_platform_stubs'}",
            f"-I{IMPL / 'src/matter_core_stage'}",
            f"-I{IMPL / 'src/platform/arduino'}",
            f"-I{IMPL / 'third_party/connectedhomeip/src'}",
            str(
                IMPL
                / "third_party/connectedhomeip/src/system/SystemPacketBuffer.cpp"
            ),
            str(ROOT / "tests/core_io/matter_inet_endpoint_test.cpp"),
            "-o",
            str(inet_output),
        ]
        subprocess.run(inet_command, check=True, cwd=ROOT)
        subprocess.run(
            [str(inet_output)], check=True, cwd=ROOT, env=sanitizer_env
        )
    print("PASS: Matter Arduino System Layer and Inet endpoint regressions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
