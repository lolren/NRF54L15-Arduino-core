#!/usr/bin/env python3
"""Contracts for non-writing Wire scanner address probes (issue #100)."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "hardware/nrf54l15clean/nrf54l15clean/cores"
WIRE_SOURCES = (
    CORE / "nrf54l15/Wire.cpp",
    CORE / "nrf54lm20b/Wire.cpp",
)
SCANNER = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/examples/Wire/WireScanner/WireScanner.ino"
)


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    assert start >= 0, f"missing function: {signature}"
    opening = text.find("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def braced_block(text: str, token: str) -> str:
    start = text.find(token)
    assert start >= 0, f"missing block: {token}"
    opening = text.find("{", start + len(token))
    assert opening >= 0, f"missing opening brace: {token}"
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError(f"unterminated block: {token}")


def assert_order(text: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        offset = text.find(token, cursor)
        assert offset >= 0, f"missing ordered token: {token}"
        cursor = offset + len(token)


def end_tx_error_code(
    transaction_ok: bool, stop_ok: bool, errorsrc: int, error_event: bool = False
) -> int:
    transaction_ok = transaction_ok and not error_event
    if transaction_ok and stop_ok and errorsrc == 0:
        return 0
    if errorsrc & (1 << 1):
        return 2
    if errorsrc & (1 << 2):
        return 3
    return 4


def test_error_mapping_model() -> None:
    assert end_tx_error_code(True, True, 0) == 0
    assert end_tx_error_code(False, True, 1 << 1) == 2
    assert end_tx_error_code(False, True, 1 << 2) == 3
    assert end_tx_error_code(False, False, 0) == 4
    assert end_tx_error_code(True, True, 0, error_event=True) == 4
    print("PASS Wire address/data NACK and timeout status model")


def test_zero_length_probe_source() -> None:
    probe_bodies: list[str] = []
    for path in WIRE_SOURCES:
        text = path.read_text(encoding="utf-8")
        assert "T_TWIM_ERRORSRC_ANACK = (1UL << 1U)" in text
        assert "T_TWIM_ERRORSRC_DNACK = (1UL << 2U)" in text
        assert "T_TWIM_SHORT_LASTTX_STOP = (1UL << 9U)" in text
        assert "T_TWIM_SHORT_LASTRX_STOP = (1UL << 12U)" in text
        status_helper = function_body(
            text,
            "static uint8_t end_tx_error_code(bool transactionOk, bool stopOk, uint32_t errorsrc)",
        )
        assert_order(
            status_helper,
            "transactionOk && stopOk && errorsrc == 0U",
            "return 0U",
            "errorsrc & T_TWIM_ERRORSRC_ANACK",
            "return 2U",
            "errorsrc & T_TWIM_ERRORSRC_DNACK",
            "return 3U",
            "return 4U",
        )
        body = function_body(text, "uint8_t TwoWire::endTransmission(bool sendStop)")
        probe = braced_block(body, "if (_txBufferLength == 0U)")
        probe_bodies.append(probe)

        assert "if (!sendStop)" in probe
        assert "return 4U;" in probe
        unsupported = braced_block(probe, "if (!sendStop)")
        assert_order(
            unsupported,
            "if (_pendingRepeatedStart)",
            "T_EVENTS_STOPPED) = 0U",
            "T_TASKS_STOP) = 1U",
            "wait_event(base, T_EVENTS_STOPPED",
            "_pendingRepeatedStart = !stopped",
            "return 4U;",
        )
        assert "reinterpret_cast<uintptr_t>(_txBuffer)" in probe
        assert "static uint8_t probeByte" not in probe
        assert "T_DMA_RX_MAXCNT) = 1U" in probe
        assert "T_TASKS_DMA_RX_START) = 1U" in probe
        assert "T_TASKS_DMA_TX_START" not in probe
        assert "T_DMA_TX_MAXCNT" not in probe
        assert_order(
            probe,
            "T_EVENTS_STOPPED) = 0U",
            "T_EVENTS_ERROR) = 0U",
            "T_EVENTS_LASTRX) = 0U",
            "T_EVENTS_DMA_RX_END) = 0U",
            "T_TWIM_ERRORSRC) = T_TWIM_ERRORSRC_ALL",
            "T_DMA_RX_PTR",
            "T_DMA_RX_MAXCNT) = 1U",
            "T_SHORTS) = T_TWIM_SHORT_LASTRX_STOP",
            "T_TASKS_DMA_RX_START) = 1U",
            "wait_event_or_error(base, T_EVENTS_LASTRX",
            "if (!addressAcked)",
            "wait_event(base, T_EVENTS_STOPPED",
            "T_SHORTS) = 0U",
            "const uint32_t errorsrc",
            "const bool errorEvent",
            "if (stopOk)",
            "T_DMA_RX_PTR) = 0U",
            "T_DMA_RX_MAXCNT) = 0U",
            "__DSB()",
            "end_tx_error_code(addressAcked && !errorEvent, stopOk, errorsrc)",
        )
        assert probe.count("T_TWIM_ERRORSRC) = T_TWIM_ERRORSRC_ALL") == 2

        regular = body[body.index(probe) + len(probe) :]
        assert_order(
            regular,
            "T_SHORTS) = sendStop ? T_TWIM_SHORT_LASTTX_STOP : 0U",
            "wait_event_or_error(base, doneEvent",
            "uint32_t errorsrc",
            "bool errorEvent",
            "T_TASKS_STOP) = 1U",
            "wait_event(base, T_EVENTS_STOPPED",
            "errorsrc |= reg32(base + T_TWIM_ERRORSRC)",
            "errorEvent = errorEvent ||",
            "T_SHORTS) = 0U",
            "end_tx_error_code(writeOk && !errorEvent, stopOk, errorsrc)",
        )

    # Board cores must not drift in scanner semantics.
    normalized = ["\n".join(line.strip() for line in body.splitlines()) for body in probe_bodies]
    assert normalized[0] == normalized[1]
    print("PASS L15 and LM20A zero-length Wire probes force a real, non-writing address phase")


def test_dual_bus_scanner_source() -> None:
    scanner = SCANNER.read_text(encoding="utf-8")
    for token in (
        "static void scanBus(TwoWire& bus, const char* name, uint8_t sda, uint8_t scl)",
        "digitalRead(sda) == LOW || digitalRead(scl) == LOW",
        'scanBus(Wire, "Wire", SDA, SCL)',
        'scanBus(Wire1, "Wire1", SDA1, SCL1)',
        "BoardControl::setImuMicEnabled(true)",
        "pinMode(PIN_IMU_CS, OUTPUT)",
        "digitalWrite(PIN_IMU_CS, HIGH)",
        "ARDUINO_XIAO_NRF54L15_CLEAN",
    ):
        assert token in scanner, f"WireScanner board-aware contract missing: {token}"
    assert scanner.count("BoardControl::setImuMicEnabled(true)") == 2
    print("PASS WireScanner powers Sense rails, selects LM20A I2C mode, and scans both buses")


def main() -> None:
    test_error_mapping_model()
    test_zero_length_probe_source()
    test_dual_bus_scanner_source()
    print("PASS all Wire address-probe contracts")


if __name__ == "__main__":
    main()
