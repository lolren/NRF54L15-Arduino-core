#!/usr/bin/env python3
"""Source contracts for foreground-to-background BLE connection handoff."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONNECTION_API = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries"
    / "Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts"
    / "nrf54l15_hal_ble_connection_api.inc"
)


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def test_enable_handoff_is_armed_before_power_down(source: str) -> None:
    body = function_body(
        source, "void BleRadio::setBackgroundConnectionServiceEnabled(bool enabled)"
    )
    enable_path = body[body.index("backgroundConnectionServiceEnabled_ = true;") :]

    arm = enable_path.index("serviceBackgroundConnection(0U);")
    stop_hfxo = enable_path.index("ClockControl::stopHfxo();")
    release_rf = enable_path.index("releaseRfPathForBle();")
    assert arm < stop_hfxo < release_rf

    assert "connected_ && backgroundConnectionServiceDue_" in enable_path
    assert "serviceBackgroundConnection(120000UL);" in enable_path
    assert "backgroundConnectionServiceArmed_ &&" in enable_path
    assert "!backgroundConnectionEventWakeArmed_" in enable_path
    power_critical = enable_path.index(
        "const uint32_t handoffPowerIrqState = bleEnterCritical();"
    )
    power_critical_end = enable_path.index(
        "bleExitCritical(handoffPowerIrqState);", power_critical
    )
    assert power_critical < stop_hfxo < release_rf < power_critical_end


def test_handoff_can_schedule_pre_warm_while_hfxo_is_running(source: str) -> None:
    body = function_body(
        source, "void BleRadio::serviceBackgroundConnection(uint32_t spinLimit)"
    )
    future_prewarm = body[
        body.index("if ((prewarmUs < eventWakeUs)") :
        body.index("if (timeReachedUs(nowUs, prewarmUs))")
    ]
    assert "!bleHfxoRunning()" not in future_prewarm
    assert "backgroundConnectionEventWakeArmed_ = false;" in future_prewarm
    assert "bleProgramCompare(kBleBackgroundConnPrewarmCompareChannel, prewarmUs" in (
        future_prewarm
    )

    active_lead_window = body[
        body.index("if (timeReachedUs(nowUs, prewarmUs))") :
        body.index("if (timeReachedUs(nowUs, eventWakeUs))")
    ]
    assert "ensureRfPathActiveForBle();" in active_lead_window


def main() -> None:
    source = CONNECTION_API.read_text(encoding="utf-8")
    test_enable_handoff_is_armed_before_power_down(source)
    test_handoff_can_schedule_pre_warm_while_hfxo_is_running(source)
    print("PASS BLE background connection handoff source contracts")


if __name__ == "__main__":
    main()
