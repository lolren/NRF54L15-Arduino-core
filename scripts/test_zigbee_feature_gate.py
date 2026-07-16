#!/usr/bin/env python3
"""Compile-time contracts for the public Zigbee feature gate."""

import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / (
    "hardware/nrf54l15clean/nrf54l15clean/libraries/"
    "Nrf54L15-Clean-Implementation/src"
)
BOARDS = ROOT / "hardware/nrf54l15clean/nrf54l15clean/boards.txt"


def compile_header(defines: tuple[str, ...]) -> subprocess.CompletedProcess[str]:
    compiler = shutil.which("g++")
    if compiler is None:
        raise RuntimeError("g++ is required for the Zigbee feature-gate contract")

    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "probe.cpp"
        source.write_text('#include "zigbee_stack.h"\nint main() { return 0; }\n')
        command = [
            compiler,
            "-std=c++17",
            "-fsyntax-only",
            "-I",
            str(SOURCE),
            *(f"-D{value}" for value in defines),
            str(source),
        ]
        return subprocess.run(command, text=True, capture_output=True, check=False)


def link_enabled(defines: tuple[str, ...]) -> subprocess.CompletedProcess[str]:
    compiler = shutil.which("g++")
    if compiler is None:
        raise RuntimeError("g++ is required for the Zigbee feature-gate contract")

    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        (temporary / "Arduino.h").write_text(
            "#pragma once\n#include <stdint.h>\nunsigned long millis();\n"
        )
        source = temporary / "probe.cpp"
        source.write_text(
            '#include "zigbee_stack.h"\n'
            "unsigned long millis() { return 0UL; }\n"
            "int main() {\n"
            "  const uint8_t frame[3] = {0U, 1U, 2U};\n"
            "  xiao_nrf54l15::ZigbeeZclFrame parsed{};\n"
            "  return xiao_nrf54l15::ZigbeeCodec::parseZclFrame(\n"
            "      frame, sizeof(frame), &parsed) ? 0 : 1;\n"
            "}\n"
        )
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-ffunction-sections",
            "-fdata-sections",
            "-I",
            str(temporary),
            "-I",
            str(SOURCE),
            *(f"-D{value}" for value in defines),
            str(SOURCE / "zigbee_stack.cpp"),
            str(source),
            "-Wl,--gc-sections",
            "-o",
            str(temporary / "probe"),
        ]
        return subprocess.run(command, text=True, capture_output=True, check=False)


def main() -> int:
    for defines in (
        ("NRF54L15_CLEAN_ZIGBEE_ENABLED=1",),
        ("NRF54L15_CLEAN_ZIGBEE_ENABLE=1",),
    ):
        result = compile_header(defines)
        assert result.returncode == 0, result.stderr
        linked = link_enabled(defines)
        assert linked.returncode == 0, linked.stderr

    for defines, message in (
        (
            (
                "NRF54L15_CLEAN_ZIGBEE_ENABLED=1",
                "NRF54L15_CLEAN_BLE_ENABLED=1",
            ),
            "shared RADIO arbitration is not complete",
        ),
        (
            (
                "NRF54L15_CLEAN_ZIGBEE_ENABLED=1",
                "NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE=1",
            ),
            "enable only one 802.15.4 stack",
        ),
    ):
        result = compile_header(defines)
        assert result.returncode != 0
        assert message in result.stderr

    for defines in (
        (),
        ("NRF54L15_CLEAN_ZIGBEE_DISABLED",),
        ("NRF54L15_CLEAN_ZIGBEE_ENABLED=0",),
        ("NRF54L15_CLEAN_ZIGBEE_ENABLE=0",),
        (
            "NRF54L15_CLEAN_ZIGBEE_ENABLED=0",
            "NRF54L15_CLEAN_ZIGBEE_DISABLED",
        ),
    ):
        result = compile_header(defines)
        assert result.returncode != 0
        assert "Zigbee stack support is disabled" in result.stderr

    boards = BOARDS.read_text(encoding="utf-8")
    board_ids = (
        "xiao_nrf54l15",
        "xiao_nrf54lm20b",
        "holyiot_25007_nrf54l15",
        "holyiot_25008_nrf54l15",
        "generic_nrf54l15_module_36pin",
        "nrf54l15dk_pca10156",
    )
    for board in board_ids:
        default = (
            f"{board}.build.zigbee_flags=-DNRF54L15_CLEAN_ZIGBEE_ENABLED=0 "
            "-DNRF54L15_CLEAN_ZIGBEE_DISABLED"
        )
        disabled = f"{board}.menu.clean_zigbee.off=Disabled (Default)"
        enabled = f"{board}.menu.clean_zigbee.on=Enabled (Experimental)"
        assert default in boards, default
        assert disabled in boards, disabled
        assert enabled in boards, enabled
        assert boards.index(disabled) < boards.index(enabled)

    print(
        "PASS Zigbee feature gate fails at compile, links when explicitly "
        "enabled, and is disabled by default"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
