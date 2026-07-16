#!/usr/bin/env python3
"""Source contracts for foreground-to-background BLE connection handoff."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PARTS = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries"
    / "Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts"
)
CONNECTION_API = PARTS / "nrf54l15_hal_ble_connection_api.inc"
ADVERTISING = PARTS / "nrf54l15_hal_ble_advertising.inc"
SCANNING = PARTS / "nrf54l15_hal_ble_scanning_connections.inc"


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


def test_rx_path_opens_only_after_clock_quality_proof(
    connection: str, advertising: str
) -> None:
    prepare = function_body(
        connection, "bool BleRadio::prepareBackgroundConnectionReceiveWindow("
    )
    close_route = prepare.index("configureBackgroundConnectionHardware(false)")
    program_compare = prepare.index(
        "bleProgramCompare(kBleBackgroundConnRxCompareChannel, rxStartUs"
    )
    mark_prepared = prepare.index("backgroundConnectionRxPrepared_ = true;")
    assert close_route < program_compare < mark_prepared
    assert "if (bleHfxoReadyForRadio())" in prepare
    ready_path = prepare[prepare.index("if (bleHfxoReadyForRadio())") :]
    assert "configureBackgroundConnectionHardware(true)" in ready_path
    assert "bleBackgroundEnableClockQualityInterrupts();" in ready_path
    assert "bleBackgroundEnableClockTuneInterrupts();" in ready_path

    clock_irq = function_body(
        advertising, 'extern "C" void nrf54l15_ble_clock_irq_service(void)'
    )
    connection_path = clock_irq[
        clock_irq.index("if (owner->connected_") :
        clock_irq.index("if (!owner->backgroundAdvertisingEnabled_")
    ]
    assert "owner->backgroundConnectionRxPrepared_" in connection_path
    not_tuned = connection_path[
        connection_path.index("if (!tuned)") :
        connection_path.index("if (!owner->configureBackgroundConnectionHardware(true))")
    ]
    assert "configureBackgroundConnectionHardware(false)" in not_tuned
    assert "bleBackgroundEnableClockTuneInterrupts();" in not_tuned
    assert "configureBackgroundConnectionHardware(true)" in connection_path
    assert "bleCompareEventPending(kBleBackgroundConnRxCompareChannel)" in (
        connection_path
    )
    late_compare = connection_path[
        connection_path.index(
            "if (bleCompareEventPending(kBleBackgroundConnRxCompareChannel))"
        ) :
    ]
    assert "radioState == RADIO_STATE_STATE_Disabled" in late_compare
    assert "owner->radio_->TASKS_RXEN" in late_compare
    assert "bleDisableCompare(kBleBackgroundConnRxCompareChannel, true)" in (
        late_compare
    )
    assert late_compare.index("radioState == RADIO_STATE_STATE_Disabled") < (
        late_compare.index("owner->radio_->TASKS_RXEN")
    )


def test_late_advertising_xotuned_rebuilds_deadlines(advertising: str) -> None:
    clock_irq = function_body(
        advertising, 'extern "C" void nrf54l15_ble_clock_irq_service(void)'
    )
    advertising_path = clock_irq[
        clock_irq.index("if (!owner->backgroundAdvertisingEnabled_") :
    ]
    assert "bleBackgroundAdvertisingSetTxPathEnabled(false)" in advertising_path
    assert "bleBackgroundAdvertisingSetTxPathEnabled(true)" in advertising_path
    assert "kBleBackgroundAdvTxCompareChannel" in advertising_path
    assert "kBleBackgroundAdvStage1TxCompareChannel" in advertising_path
    assert "kBleBackgroundAdvStage2TxCompareChannel" in advertising_path
    assert "bleCompareEventPending(compareChannel)" in advertising_path
    assert "kBleBackgroundAdvTxKickRetryLimit" in advertising_path
    assert "bleTimingUs() + kBleBackgroundAdvTxKickRetryUs" in advertising_path
    assert "bleProgramCompare(compareChannel, retryTxUs, false)" in advertising_path

    # A missed TX compare cannot simply be replayed after its old XOSTOP. The
    # one-channel cleanup and every remaining three-channel stage are moved to
    # deadlines derived from retryTxUs.
    for token in (
        "bleProgramCompare(kBleBackgroundAdvCleanupCompareChannel, cleanupUs, true)",
        "bleProgramCompare(kBleBackgroundAdvStage1ServiceCompareChannel",
        "bleProgramCompare(kBleBackgroundAdvStage1TxCompareChannel",
        "bleProgramCompare(kBleBackgroundAdvStage2ServiceCompareChannel",
        "bleProgramCompare(kBleBackgroundAdvStage2TxCompareChannel",
        "bleProgramCompare(kBleBackgroundAdvFinalCleanupCompareChannel",
        "owner->backgroundAdvertisingNextTxUs_ = retryTxUs",
        "bleBackgroundEnableClockQualityInterrupts();",
    ):
        assert token in advertising_path


def test_cleanup_retains_resources_until_radio_disabled(
    connection: str, advertising: str, scanning: str
) -> None:
    service = function_body(
        connection, "void BleRadio::serviceBackgroundConnection(uint32_t spinLimit)"
    )
    settle = service[service.rindex("bool radioDisabled =") :]
    assert "radio_->TASKS_DISABLE" in settle
    assert "waitRadioStateDisabledBudgeted(" in settle
    assert "waitDisabled(" not in settle
    failure = settle[settle.index("if (!radioDisabled)") :]
    failure = failure[: failure.index("if (manageConnectionHfxo")]
    assert "initialized_ = false;" in failure
    assert "ClockControl::stopHfxo" not in failure
    assert "releaseRfPathForBle" not in failure
    assert settle.index("if (!radioDisabled)") < settle.index(
        "ClockControl::stopHfxo"
    )
    assert settle.index("if (!radioDisabled)") < settle.index(
        "releaseRfPathForBle"
    )

    stop_connection = function_body(
        connection, "void BleRadio::stopBackgroundConnectionService()"
    )
    disabled_release = stop_connection[stop_connection.index("if (radioDisabled)") :]
    assert "releaseRfPathForBle();" in disabled_release
    assert "initialized_ = false;" in disabled_release

    prewarm = function_body(
        advertising, "bool BleRadio::handleBackgroundAdvertisingPrewarmEvent()"
    )
    assert prewarm.index("bleBackgroundAdvertisingAssertManagedLatency(true") < (
        prewarm.index("ClockControl::startHfxo(false, 0U)")
    )
    assert "backgroundAdvertisingLatencyLeaseOwned_ = true" in prewarm

    quiesce = function_body(
        advertising,
        "bool BleRadio::quiesceBackgroundAdvertisingForConnectionHandoff()",
    )
    assert "waitRadioStateDisabledBudgeted(radio_, 3000U, 120000UL)" in quiesce
    assert "waitDisabled(" not in quiesce
    assert "if (radioDisabled && backgroundAdvertisingLatencyLeaseOwned_)" in (
        quiesce
    )
    assert "return radioDisabled;" in quiesce
    stop_advertising = function_body(
        advertising, "void BleRadio::stopBackgroundAdvertising()"
    )
    power_release = stop_advertising[
        stop_advertising.index("if (radioDisabled)") :
        stop_advertising.index("const uint32_t irqState")
    ]
    assert "ClockControl::stopHfxo();" in power_release
    assert "backgroundAdvertisingLatencyLeaseOwned_" in power_release

    for signature in (
        "bool BleRadio::startConnectionFromConnectInd(",
        "bool BleRadio::startCentralConnection(",
    ):
        initiate = function_body(scanning, signature)
        assert "if (!quiesceBackgroundAdvertisingForConnectionHandoff())" in (
            initiate
        )
        handoff = initiate[
            initiate.index("if (!quiesceBackgroundAdvertisingForConnectionHandoff())") :
        ]
        assert handoff.index("return false;") < handoff.index("connected_ = true;")


def main() -> None:
    connection = CONNECTION_API.read_text(encoding="utf-8")
    advertising = ADVERTISING.read_text(encoding="utf-8")
    scanning = SCANNING.read_text(encoding="utf-8")
    test_enable_handoff_is_armed_before_power_down(connection)
    test_handoff_can_schedule_pre_warm_while_hfxo_is_running(connection)
    test_rx_path_opens_only_after_clock_quality_proof(connection, advertising)
    test_late_advertising_xotuned_rebuilds_deadlines(advertising)
    test_cleanup_retains_resources_until_radio_disabled(
        connection, advertising, scanning
    )
    print("PASS BLE background connection handoff source contracts")


if __name__ == "__main__":
    main()
