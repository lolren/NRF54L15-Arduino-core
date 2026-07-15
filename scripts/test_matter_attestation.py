#!/usr/bin/env python3
"""Focused native and source contracts for the test attestation model."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
IMPLEMENTATION = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries"
    / "Nrf54L15-Clean-Implementation"
)
SRC = IMPLEMENTATION / "src"


def require_contracts() -> None:
    header = (SRC / "matter_device_attestation.h").read_text(encoding="utf-8")
    node = (SRC / "matter_onnetwork_onoff_light.cpp").read_text(encoding="utf-8")

    for statement in (
        "This is not a Matter certificate encoding",
        "not suitable for Matter certification",
        "process-local keys are regenerated on every boot",
    ):
        if statement not in header:
            raise AssertionError(f"missing test-only attestation warning: {statement}")

    final_identity = node.index("// The test DAC must describe the final configured")
    restored_identity = node.index("loadPersistentIdentity(&restoredIdentity)")
    chain_generation = node.index("attestation_.generateTestChain(", final_identity)
    if not restored_identity < final_identity < chain_generation:
        raise AssertionError("test attestation must follow final identity selection")

    failure_name = node.index('"test_attestation_init_failed"', chain_generation)
    persistent_write = node.index("if (!savePersistentIdentity())", chain_generation)
    if not chain_generation < failure_name < persistent_write:
        raise AssertionError("begin must fail closed before persisting an unattested identity")

    set_identity = node.index("Nrf54MatterOnNetworkOnOffLightNode::setIdentity(")
    replacement = node.index("replacementAttestation.generateTestChain(", set_identity)
    assign_identity = node.index("identity_ = identity;", replacement)
    assign_attestation = node.index("attestation_ = replacementAttestation;", assign_identity)
    if not replacement < assign_identity < assign_attestation:
        raise AssertionError("identity and replacement attestation update is not atomic")

    end_method = node.index("Nrf54MatterOnNetworkOnOffLightNode::end()")
    next_method = node.index("Nrf54MatterOnNetworkOnOffLightNode::process()", end_method)
    end_body = node[end_method:next_method]
    for reset in (
        "attestation_ = MatterDeviceAttestation{};",
        "attestationReady_ = false;",
    ):
        if reset not in end_body:
            raise AssertionError(f"end does not clear attestation lifecycle state: {reset}")


def run_native_test() -> None:
    source = SRC / "matter_device_attestation.cpp"
    test = ROOT / "tests/core_io/matter_device_attestation_test.cpp"
    stubs = ROOT / "tests/matter_platform_stubs"

    with tempfile.TemporaryDirectory(prefix="matter-attestation-") as directory:
        binary = Path(directory) / "matter_device_attestation_test"
        command = [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-no-pie",
            "-DNRF54L15_CLEAN_MATTER_CORE_ENABLE=1",
            f"-I{stubs}",
            f"-I{SRC}",
            str(source),
            str(test),
            "-o",
            str(binary),
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


def main() -> int:
    require_contracts()
    run_native_test()
    print("matter_attestation_contracts=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
