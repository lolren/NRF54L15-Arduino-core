#!/usr/bin/env python3
"""Contract tests for L15/LM20 Arduino build-cache isolation."""

from __future__ import annotations

import importlib.util
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware" / "nrf54l15clean" / "nrf54l15clean"
GUARD_SCRIPT = PLATFORM / "tools" / "build_cache_guard.py"


def load_guard_module():
    spec = importlib.util.spec_from_file_location("build_cache_guard", GUARD_SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_cache_cleanup() -> None:
    guard = load_guard_module()
    with tempfile.TemporaryDirectory() as directory:
        build = Path(directory)
        platform = build / "platform-1"
        platform.mkdir()
        (build / "core").mkdir()
        (build / "libraries" / "Example").mkdir(parents=True)
        (build / "sketch").mkdir()
        (build / "core" / "core.a").write_bytes(b"stale")
        (build / "libraries" / "Example" / "object.o").write_bytes(b"stale")
        (build / "libraries" / "Example" / "object.d").write_text(
            "C:\\core\\cores\\nrf54lm20b\\nrf54l15.h\n", encoding="ascii"
        )
        (build / "sketch" / "sketch.cpp.o").write_bytes(b"stale")
        (build / "sketch" / "sketch.cpp").write_text("// keep\n", encoding="ascii")
        (build / "firmware.elf").write_bytes(b"stale")

        assert guard.clean_build_cache(build, "nrf54l15", platform) == (
            "existing cache predates target tracking"
        )
        assert not (build / "core").exists()
        assert not (build / "libraries").exists()
        assert not (build / "sketch" / "sketch.cpp.o").exists()
        assert (build / "sketch" / "sketch.cpp").is_file()
        assert not (build / "firmware.elf").exists()
        marker_lines = (build / guard.MARKER_NAME).read_text().splitlines()
        assert marker_lines == ["nrf54l15", str(platform.resolve())]

        assert guard.clean_build_cache(build, "nrf54l15", platform) is None
        assert guard.clean_build_cache(build, "nrf54lm20b", platform) == (
            "target changed from nrf54l15 to nrf54lm20b"
        )

        platform2 = build / "platform-2"
        platform2.mkdir()
        assert guard.clean_build_cache(build, "nrf54lm20b", platform2) == (
            "platform installation changed"
        )
    print("PASS cross-board dependency and marker cleanup")


def validate_link_contracts() -> None:
    platform = (PLATFORM / "platform.txt").read_text()
    assert platform.count("BuildTargetGuard.h") == 3
    assert "recipe.hooks.prebuild.1.pattern=" in platform
    assert "recipe.hooks.prebuild.1.pattern.windows=" in platform

    expectations = {
        "nrf54l15": "__nrf54_core_target_nrf54l15",
        "nrf54lm20b": "__nrf54_core_target_nrf54lm20",
    }
    for core, symbol in expectations.items():
        header = (PLATFORM / "cores" / core / "BuildTargetGuard.h").read_text()
        assert symbol in header
        assert ".gnu.linkonce.r.nrf54_core_target_guard." in header

    l15_linkers = (
        PLATFORM / "cores" / "nrf54l15" / "nrf54l15_linker_script.ld",
        PLATFORM / "cores" / "nrf54l15" / "nrf54l15_linker_script_no_vpr.ld",
    )
    lm20_linkers = (
        PLATFORM / "cores" / "nrf54lm20b" / "nrf54lm20b_linker_script.ld",
        PLATFORM / "cores" / "nrf54l15" / "nrf54lm20b_linker_script.ld",
    )
    for path in l15_linkers:
        text = path.read_text()
        assert "PROVIDE(__nrf54_core_target_nrf54l15" in text
        assert "KEEP(*(.gnu.linkonce.r.nrf54_core_target_guard.*))" in text
    for path in lm20_linkers:
        text = path.read_text()
        assert "PROVIDE(__nrf54_core_target_nrf54lm20" in text
        assert "KEEP(*(.gnu.linkonce.r.nrf54_core_target_guard.*))" in text
    print("PASS linker-enforced per-object SoC identity")


if __name__ == "__main__":
    validate_cache_cleanup()
    validate_link_contracts()
    print("PASS all cross-board build guard contracts")
