#!/usr/bin/env python3
"""Validate that unsupported BLE periodic advertising fails closed."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LIBRARY = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries"
    / "Nrf54L15-Clean-Implementation"
)


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def main() -> int:
    header = (LIBRARY / "src/nrf54l15_hal_ble_periodic.h").read_text(
        encoding="utf-8"
    )
    example = (
        LIBRARY
        / "examples/Peripherals/BlePeriodicAdvertising/BlePeriodicAdvertising.ino"
    ).read_text(encoding="utf-8")
    catalog = (LIBRARY / "src/nrf54l15_hal_parts/NEW_FEATURES.md").read_text(
        encoding="utf-8"
    )

    assert "static constexpr bool supported() { return false; }" in header
    for signature in (
        "inline bool begin(",
        "inline bool setData(",
        "inline bool setIntervalMs(",
        "inline bool setTxPowerDbm(",
    ):
        operation = body(header, signature)
        assert "return false;" in operation
        assert "return true;" not in operation
    assert "inline bool isActive() const { return false; }" in header
    assert "inline uint32_t packetCount() const { return 0U; }" in header

    for contract in (
        "static_assert(!BlePeriodicAdvertising::supported()",
        "Support: NOT IMPLEMENTED",
        "Fail-closed API check",
        "No periodic advertising packets were transmitted.",
    ):
        assert contract in example
    for misleading_claim in (
        "Transmits periodic advertising PDU packets",
        "Use a BLE scanner",
        "to observe packets",
    ):
        assert misleading_claim not in example

    assert "Unsupported capability stub (fails closed)" in catalog
    assert "Unsupported capability probe (no radio transmission)" in catalog
    assert "Periodic advertising uses raw RADIO" not in catalog
    print("PASS unsupported periodic advertising is explicit and fail-closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
