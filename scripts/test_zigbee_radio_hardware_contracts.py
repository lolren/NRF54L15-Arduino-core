#!/usr/bin/env python3
"""Datasheet-level source contracts for nRF54 802.15.4 radio prerequisites."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
IMPLEMENTATION = (
    PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"
)
PARTS = IMPLEMENTATION / "nrf54l15_hal_parts"
RAW = PARTS / "nrf54l15_hal_802154_rawradio.inc"
TIMEBASE = IMPLEMENTATION / "nrf54l15_hal_timebase.cpp"
OSCILLATORS = IMPLEMENTATION / "nrf54l15_hal_oscillators.h"
HEADER = IMPLEMENTATION / "nrf54l15_hal.h"
BLE_ADVERTISING = PARTS / "nrf54l15_hal_ble_advertising.inc"
BLE_CONNECTION = PARTS / "nrf54l15_hal_ble_connection_api.inc"
BLE_SCANNING = PARTS / "nrf54l15_hal_ble_scanning_connections.inc"
BLE_TIMING = PARTS / "nrf54l15_hal_internal_ble_timing.inc"
CRYPTO_ANALOG = PARTS / "nrf54l15_hal_crypto_analog.inc"
OPENTHREAD = IMPLEMENTATION / "openthread_platform_nrf54l15.cpp"
SERIAL_CONSTLAT_PROVIDERS = (
    PLATFORM / "cores/nrf54l15/HardwareSerial.cpp",
    PLATFORM / "cores/nrf54lm20b/HardwareSerial.cpp",
)
SYSTEM_OFF = (
    PLATFORM / "cores/nrf54l15/wiring_time.c",
    PLATFORM / "cores/nrf54lm20b/wiring_time.c",
)


def source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    opening = text.find("{", start + len(signature))
    if opening < 0:
        raise AssertionError(f"missing opening brace: {signature}")
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def assert_order(text: str, *tokens: str) -> None:
    offsets = [text.find(token) for token in tokens]
    missing = [token for token, offset in zip(tokens, offsets) if offset < 0]
    assert not missing, f"missing ordered tokens: {missing}"
    assert offsets == sorted(offsets), f"wrong order for tokens: {tokens}"


def test_hfxo_radio_readiness_and_anomaly_39() -> None:
    timebase = source(TIMEBASE)
    start = function_body(timebase, "bool ClockControl::startHfxo(")
    stop = function_body(timebase, "void ClockControl::stopHfxo()")

    assert "g_hfxoTunedObserved" not in timebase
    assert "clock->EVENTS_XOTUNED != 0U" in start
    assert "clock->EVENTS_XOTUNEERROR == 0U" in start
    assert "clock->EVENTS_XOTUNEFAILED == 0U" in start
    assert "clock->TASKS_XOTUNE" in start
    assert "radioState != RADIO_STATE_STATE_Disabled" in start
    assert_order(start, "clock->TASKS_XOTUNE", "if (!waitForTuned)")
    assert_order(start, "clock->TASKS_PLLSTART", "clock->TASKS_XOSTART")
    wait_loop = start[start.index("while (spinLimit-- > 0U)") :]
    assert "const bool tuned = clock->EVENTS_XOTUNED != 0U" in wait_loop
    assert "const bool tuneError = clock->EVENTS_XOTUNEERROR != 0U" in wait_loop
    assert "const bool tuneFailed = clock->EVENTS_XOTUNEFAILED != 0U" in wait_loop
    assert "tuned && !tuneError && !tuneFailed" in wait_loop
    assert "tuneRetriesRemaining == 0U" in wait_loop
    assert "radioState != RADIO_STATE_STATE_Disabled" in wait_loop
    assert "clock->TASKS_XOTUNE" in wait_loop
    assert "EVENTS_XOSTARTED != 0U" not in wait_loop
    assert "CLOCK_XO_STAT_STATE_Running" not in wait_loop
    assert_order(stop, "clock->TASKS_XOSTOP", "clock->TASKS_PLLSTOP")

    oscillators = source(OSCILLATORS)
    generic_start = function_body(oscillators, "inline static void startHfclk()")
    generic_stop = function_body(oscillators, "inline static void stopHfclk()")
    assert_order(generic_start, "startPll();", "startHfxo();")
    assert_order(generic_stop, "stopHfxo();", "stopPll();")

    for path in SYSTEM_OFF:
        body = function_body(source(path), "static bool stopHfxoForSystemOff(void)")
        running_path = body[body.index("NRF_CLOCK->TASKS_XOSTOP") :]
        assert_order(
            running_path,
            "NRF_CLOCK->TASKS_XOSTOP",
            "NRF_CLOCK->TASKS_PLLSTOP",
        )
        not_running = body[: body.index("NRF_CLOCK->TASKS_XOSTOP")]
        assert "NRF_CLOCK->TASKS_PLLSTOP" in not_running

    print("PASS XOTUNED readiness and anomaly-39 start/stop ordering")


def test_ble_background_clock_sequence_and_dppi_ownership() -> None:
    advertising = source(BLE_ADVERTISING)
    timing = source(BLE_TIMING)
    configure = function_body(
        advertising,
        "bool BleRadio::configureBackgroundAdvertisingHardware(bool enable)",
    )
    arm = function_body(advertising, "bool BleRadio::armBackgroundAdvertising(")
    prewarm = function_body(
        advertising, "bool BleRadio::handleBackgroundAdvertisingPrewarmEvent()"
    )
    clock_irq = function_body(
        advertising, 'extern "C" void nrf54l15_ble_clock_irq_service(void)'
    )
    service = function_body(advertising, "bool BleRadio::serviceBackgroundAdvertising(")
    stop = function_body(advertising, "void BleRadio::stopBackgroundAdvertising()")

    assert "const bool prewarmInterrupt = true;" in arm
    assert "&NRF_CLOCK->SUBSCRIBE_PLLSTART" in configure
    # The prewarm compare starts PLL only. Its mandatory IRQ then calls
    # ClockControl::startHfxo(), preserving anomaly-39 PLLSTART -> XOSTART
    # program order instead of subscribing both CLOCK tasks to one DPPI pulse.
    enable_path = configure[configure.index("// MLTPAN-39 requires PLLSTART") :]
    assert "SUBSCRIBE_XOSTART" not in enable_path
    assert "configureSubscribe(&NRF_CLOCK->SUBSCRIBE_XOSTOP" not in enable_path
    assert "connect(&NRF_CLOCK->SUBSCRIBE_XOSTOP" not in enable_path
    assert "disconnectSubscribe(&NRF_CLOCK->SUBSCRIBE_XOSTOP)" in enable_path
    assert "enableChannel(kBleBackgroundAdvLpStopDppiChannel, false)" in enable_path
    assert "enableChannel(kBleBackgroundAdvLpTxDppiChannel, false)" in configure
    assert "bleBackgroundAdvertisingSetTxPathEnabled(false)" in arm
    assert "ClockControl::startHfxo(false, 0U)" in prewarm
    assert "bleHfxoReadyForRadio()" in prewarm
    assert "bleBackgroundEnableClockTuneInterrupts();" in prewarm
    prewarm_tail = prewarm[prewarm.index("bleExitCritical(irqState);") :]
    assert (
        "if (backgroundAdvertisingUseIrqTxKick_)" in prewarm_tail
        and "bleBackgroundDisableClockIrq();" in prewarm_tail
    )
    assert prewarm_tail.count("bleBackgroundDisableClockIrq();") == 1
    assert "bleHfxoReadyForRadio()" in clock_irq
    ready = function_body(timing, "static inline bool bleHfxoReadyForRadio()")
    assert "NRF_CLOCK->EVENTS_XOTUNED != 0U" in ready
    assert "NRF_CLOCK->EVENTS_XOTUNEERROR == 0U" in ready
    assert "NRF_CLOCK->EVENTS_XOTUNEFAILED == 0U" in ready
    assert "backgroundAdvertisingAwaitingHfxoTuned_ = !tuned" in clock_irq
    assert "bleBackgroundAdvertisingSetTxPathEnabled(false)" in clock_irq
    assert "tuneError && !tuneFailed" in clock_irq
    assert "ClockControl::startHfxo(false, 0U)" in clock_irq
    assert "bleBackgroundEnableClockTuneInterrupts();" in clock_irq
    advertising_clock_path = clock_irq[
        clock_irq.index("owner->backgroundAdvertisingAwaitingHfxoTuned_") :
    ]
    assert_order(
        advertising_clock_path,
        "if (!tuned)",
        "bleBackgroundAdvertisingSetTxPathEnabled(false)",
        "return;",
        "bleBackgroundAdvertisingSetTxPathEnabled(true)",
    )
    assert "bleBackgroundEnableClockQualityInterrupts();" in clock_irq
    assert "ClockControl::stopHfxo();" in stop

    cleanup = service[service.index("eventCompleteCount") :]
    assert_order(
        cleanup,
        "ClockControl::stopHfxo();",
        "armBackgroundAdvertising(nextEventStartUs",
    )
    assert "cleanup compare was disabled on IRQ entry" in cleanup

    expected_channels = {
        "kBleBackgroundAdvLpPrewarmDppiChannel": "0U",
        "kBleBackgroundAdvLpStopDppiChannel": "2U",
        "kBleBackgroundAdvLpStopEnableDppiChannel": "3U",
        "kBleBackgroundConnLpRxDppiChannel": "4U",
    }
    for name, value in expected_channels.items():
        assert re.search(
            rf"static constexpr uint8_t {name}\s*=\s*{value};", timing
        ), f"unexpected LP DPPI allocation for {name}"
    assert "NRF54L15_CLEAN_BLE_BACKGROUND_LP_TX_DPPI_CHANNEL 1" in timing
    assert (
        "kBleBackgroundConnLpPrewarmDppiChannel =\n"
        "    kBleBackgroundAdvLpPrewarmDppiChannel"
    ) in timing

    adv_begin = function_body(
        advertising, "bool BleRadio::beginBackgroundAdvertising(uint32_t intervalMs"
    )
    assert "stopBackgroundConnectionService();" in adv_begin
    assert "connected_" in adv_begin

    dppic30_users: set[str] = set()
    for path in IMPLEMENTATION.rglob("*"):
        if path.suffix not in {".cpp", ".h", ".inc", ".c"}:
            continue
        if "DPPIC30_BASE" in source(path):
            dppic30_users.add(path.name)
    assert dppic30_users == {
        "nrf54l15_regs.h",
        "nrf54l15_hal_ble_advertising.inc",
        "nrf54l15_hal_ble_connection_api.inc",
        "nrf54l15_hal_internal_ble_timing.inc",
    }, f"unreviewed DPPIC30 owner: {sorted(dppic30_users)}"

    connection = source(BLE_CONNECTION)
    assert "kBleBackgroundConnLpRxDppiChannel" in connection
    print("PASS BLE prewarm ordering and reviewed DPPIC30 ownership")


def test_radio_errata_predicates_and_access() -> None:
    raw = source(RAW)
    configure = function_body(raw, "bool ZigbeeRadio::configureIeee802154()")
    workaround = function_body(raw, "bool applyIeee802154RadioErrata()")
    anomaly6 = function_body(raw, "bool radioAnomaly6Applies()")
    anomaly20 = function_body(raw, "bool radioAnomaly20Applies()")

    for token in (
        "0x00FFC340UL",
        "0x00FFC344UL",
        "0x1CUL",
        "0x29UL",
        "0x33UL",
    ):
        assert token in raw
    assert "part == kNrf54l15ErrataPart" in anomaly6
    assert "part == kNrf54lm20aErrataPart && revision == 0x00UL" in anomaly6
    assert "kNrf54lm20bErrataPart" not in anomaly6
    for part in (
        "kNrf54l15ErrataPart",
        "kNrf54lm20aErrataPart",
        "kNrf54lm20bErrataPart",
    ):
        assert part in anomaly20

    assert "#if defined(NRF_TRUSTZONE_NONSECURE)" in workaround
    nonsecure = workaround.split("#if defined(NRF_TRUSTZONE_NONSECURE)", 1)[1]
    assert_order(nonsecure, "return false;", "kSecureRadioBase")
    assert "kMltpan6RegisterOffset" in workaround
    assert "kMltpan6Ieee802154Value" in workaround
    assert "return *workaround == kMltpan6Ieee802154Value" in workaround
    assert_order(
        configure,
        "radio_->MODE =",
        "applyIeee802154RadioErrata()",
        "uint32_t pcnf0",
    )
    print("PASS dynamic MLTPAN-6/20 predicates and secure access policy")


def test_constant_latency_lifecycle() -> None:
    raw = source(RAW)
    header = source(HEADER)
    timing = source(BLE_TIMING)
    crypto = source(CRYPTO_ANALOG)
    acquire = function_body(raw, "bool ZigbeeRadio::acquireRadioConstantLatency()")
    release = function_body(raw, "void ZigbeeRadio::releaseRadioConstantLatency()")
    end = function_body(raw, "void ZigbeeRadio::end()")

    # Every owner goes through one counted ABI. The strong core provider and
    # serial-free weak fallback must both perform only 0->1 CONSTLAT and 1->0
    # LOWPWR hardware transitions under a PRIMASK-protected counter update.
    for provider_path in SERIAL_CONSTLAT_PROVIDERS:
        provider = source(provider_path)
        provider_acquire = function_body(
            provider, 'extern "C" uint8_t nrf54l15_constlat_acquire(void)'
        )
        provider_release = function_body(
            provider, 'extern "C" void nrf54l15_constlat_release(void)'
        )
        assert "static volatile uint16_t g_sharedConstlatUsers = 0U;" in provider
        assert "g_sharedConstlatUsers == UINT16_MAX" in provider_acquire
        assert "g_sharedConstlatUsers == 0U" in provider_acquire
        assert "NRF_POWER->TASKS_CONSTLAT" in provider_acquire
        assert provider_acquire.index("__disable_irq();") < provider_acquire.index(
            "NRF_POWER->TASKS_CONSTLAT"
        )
        assert provider_acquire.index("NRF_POWER->TASKS_CONSTLAT") < (
            provider_acquire.index("++g_sharedConstlatUsers")
        )
        assert provider_acquire.index("++g_sharedConstlatUsers") < (
            provider_acquire.rindex("__set_PRIMASK(primask)")
        )
        assert "--g_sharedConstlatUsers" in provider_release
        assert "g_sharedConstlatUsers == 0U" in provider_release
        assert "NRF_POWER->TASKS_LOWPWR" in provider_release
        assert_order(
            provider_release,
            "__disable_irq();",
            "--g_sharedConstlatUsers",
            "NRF_POWER->TASKS_LOWPWR",
            "__set_PRIMASK(primask)",
        )

    weak_acquire = function_body(
        timing,
        'extern "C" uint8_t __attribute__((weak)) nrf54l15_constlat_acquire(void)',
    )
    weak_release = function_body(
        timing,
        'extern "C" void __attribute__((weak)) nrf54l15_constlat_release(void)',
    )
    assert "static volatile uint16_t g_fallbackConstlatUsers = 0U;" in timing
    assert "g_fallbackConstlatUsers == UINT16_MAX" in weak_acquire
    assert "g_fallbackConstlatUsers == 0U" in weak_acquire
    assert "NRF_POWER->TASKS_CONSTLAT" in weak_acquire
    assert "--g_fallbackConstlatUsers" in weak_release
    assert "g_fallbackConstlatUsers == 0U" in weak_release
    assert "NRF_POWER->TASKS_LOWPWR" in weak_release

    # Zigbee nests local operations but owns exactly one shared lease. It must
    # not infer ownership from CONSTLATSTAT or drive POWER directly.
    assert "radioAnomaly20Applies()" in acquire
    assert "__disable_irq();" in acquire
    assert "constantLatencyOwnedByZigbee_ = nrf54l15_constlat_acquire() != 0U" in acquire
    assert "constantLatencyDepth_" in acquire
    assert "constantLatencyDepth_ == UINT16_MAX" in acquire
    overflow_guard = acquire[acquire.index("constantLatencyDepth_ == UINT16_MAX") :]
    assert_order(overflow_guard, "return false;", "++constantLatencyDepth_")
    assert "NRF_POWER->" not in acquire
    assert "nrf54l15_constlat_release();" in release
    assert "NRF_POWER->" not in release
    assert "constantLatencyOwnedByZigbee_ = false" in release
    disabled_cleanup = end[end.index("if (disabled)") :]
    assert "while (constantLatencyDepth_ != 0U)" in disabled_cleanup
    assert_order(
        disabled_cleanup,
        "while (constantLatencyDepth_ != 0U)",
        "collapseRfPathIdle()",
        "releaseOwnedRawRadioHfxo(&hfxoOwnedByZigbee_)",
    )

    scoped_operations = (
        "bool ZigbeeRadio::waitForMacAcknowledgement(",
        "bool ZigbeeRadio::transmit(const uint8_t* psdu",
        "bool ZigbeeRadio::transmitThenReceive(",
        "bool ZigbeeRadio::sampleEnergyDetect(",
    )
    for signature in scoped_operations:
        body = function_body(raw, signature)
        acquire_guard = body[body.index("if (!acquireRadioConstantLatency())") :]
        assert_order(acquire_guard, "return false;", "releaseRadioConstantLatency();")
        assert "releaseRadioConstantLatency();" in body, signature

    begin_rx = function_body(raw, "bool ZigbeeRadio::beginReceive(")
    cancel_rx = function_body(raw, "void ZigbeeRadio::cancelReceive(")
    assert "return beginBufferedReceive(spinLimit);" in begin_rx
    assert "releaseRadioConstantLatency();" in cancel_rx
    begin_buffered = function_body(raw, "bool ZigbeeRadio::beginBufferedReceive(")
    cancel_buffered = function_body(raw, "void ZigbeeRadio::cancelBufferedReceive(")
    assert "if (!acquireRadioConstantLatency())" in begin_buffered
    buffered_acquire = begin_buffered[
        begin_buffered.index("if (!acquireRadioConstantLatency())") :
    ]
    assert_order(buffered_acquire, "return false;", "radio_->TASKS_RXEN")
    assert "releaseRadioConstantLatency();" in cancel_buffered

    # RAII owners cannot be copied. A hardware-fault destructor path may
    # deliberately quarantine a lease, which prevents an unsafe LOWPWR write
    # while RADIO has failed to reach Disabled.
    for class_name in ("PowerManager", "ZigbeeRadio", "RawRadioLink"):
        class_start = header.index(f"class {class_name}")
        class_end = header.index("};", class_start)
        declaration = header[class_start:class_end]
        assert f"{class_name}(const {class_name}&) = delete;" in declaration
        assert (
            f"{class_name}& operator=(const {class_name}&) = delete;"
            in declaration
        )
    power_destructor = function_body(crypto, "PowerManager::~PowerManager()")
    quarantine = function_body(
        crypto, "void PowerManager::quarantineConstantLatencyLease()"
    )
    assert "nrf54l15_constlat_release();" in power_destructor
    assert "constantLatencyOwned_ = false;" in quarantine
    assert "nrf54l15_constlat_release" not in quarantine
    raw_destructor = function_body(raw, "RawRadioLink::~RawRadioLink()")
    assert "if (!rawRadioDisabled(radio_))" in raw_destructor
    assert "power_.quarantineConstantLatencyLease();" in raw_destructor

    # Background advertising must use the same counted lease; the old direct
    # POWER.SUBSCRIBE_CONSTLAT DPPI path could bypass other owners.
    advertising = source(BLE_ADVERTISING)
    assert "SUBSCRIBE_CONSTLAT" not in advertising
    prewarm = function_body(
        advertising, "bool BleRadio::handleBackgroundAdvertisingPrewarmEvent()"
    )
    assert "backgroundAdvertisingLatencyLeaseOwned_" in prewarm
    assert "bleBackgroundAdvertisingAssertManagedLatency(true" in prewarm
    assert "NRF_POWER->TASKS_CONSTLAT" not in prewarm

    print("PASS shared counted anomaly-20 constant-latency ownership lifecycle")


def test_raw_radio_hfxo_ownership_lifecycle() -> None:
    raw = source(RAW)
    header = source(HEADER)
    zigbee_begin = function_body(raw, "bool ZigbeeRadio::begin(")
    zigbee_end = function_body(raw, "void ZigbeeRadio::end()")
    raw_begin = function_body(raw, "bool RawRadioLink::begin(")
    raw_end = function_body(raw, "void RawRadioLink::end()")
    zigbee_configure = function_body(raw, "bool ZigbeeRadio::configureIeee802154()")
    raw_configure = function_body(raw, "bool RawRadioLink::configureProprietary1M()")

    assert "hfxoOwnedByZigbee_" in header
    assert "hfxoOwnedByRawRadio_" in header
    assert "const bool hfxoWasRunning = rawRadioHfxoRunning();" in zigbee_begin
    assert "hfxoOwnedByZigbee_ || !hfxoWasRunning" in zigbee_begin
    assert "releaseOwnedRawRadioHfxo(&hfxoOwnedByZigbee_)" in zigbee_begin
    assert "releaseOwnedRawRadioHfxo(&hfxoOwnedByZigbee_)" in zigbee_end
    zigbee_disabled_cleanup = zigbee_end[zigbee_end.index("if (disabled)") :]
    assert "releaseOwnedRawRadioHfxo(&hfxoOwnedByZigbee_)" in zigbee_disabled_cleanup
    assert "releaseOwnedRawRadioHfxo(&hfxoOwnedByZigbee_)" not in (
        zigbee_end[zigbee_end.index("const bool disabled") : zigbee_end.index("if (disabled)")]
    )

    assert "const bool hfxoWasRunning = rawRadioHfxoRunning();" in raw_begin
    assert "hfxoOwnedByRawRadio_ || !hfxoWasRunning" in raw_begin
    assert "releaseOwnedRawRadioHfxo(&hfxoOwnedByRawRadio_)" in raw_begin
    assert "releaseOwnedRawRadioHfxo(&hfxoOwnedByRawRadio_)" in raw_end
    assert_order(
        zigbee_configure,
        "radio_->TASKS_DISABLE",
        "waitRadioStateDisabledBudgeted",
        "if (!disabled)",
        "radio_->TASKS_SOFTRESET",
    )
    raw_disable = raw_configure.index("radio_->TASKS_DISABLE")
    raw_wait = raw_configure.index("waitRadioStateDisabledBudgeted", raw_disable)
    raw_softreset = raw_configure.index("radio_->TASKS_SOFTRESET", raw_wait)
    assert raw_disable < raw_wait < raw_softreset
    assert "return false;" in raw_configure[raw_wait:raw_softreset]

    # SYSTEMOFF may force LOWPWR only after every software/hardware owner has
    # successfully quiesced and anomaly-39 HFXO shutdown has completed.
    for path in SYSTEM_OFF:
        systemoff = source(path)
        entry = function_body(
            systemoff,
            "static void enterSystemOffInternal(bool disableRamRetention",
        )
        assert_order(
            entry,
            "core_prepare_system_off()",
            "quiesceSystemOffDmaOwners()",
            "stopHfxoForSystemOff()",
            "NRF_POWER->TASKS_LOWPWR",
            "NRF_REGULATORS->SYSTEMOFF",
        )
    print("PASS fail-safe Zigbee/raw-radio HFXO and SYSTEMOFF ownership lifecycle")


def test_tifs_units_and_energy_detect_sequence() -> None:
    raw = source(RAW)
    header = source(HEADER)
    openthread = source(OPENTHREAD)
    configure = function_body(raw, "bool ZigbeeRadio::configureIeee802154()")
    energy = function_body(raw, "bool ZigbeeRadio::sampleEnergyDetect(")
    begin_energy = function_body(raw, "bool ZigbeeRadio::beginEnergyDetectScan(")
    poll_energy = function_body(raw, "bool ZigbeeRadio::pollEnergyDetectScan(")
    cca_setter = function_body(
        raw, "bool ZigbeeRadio::setCcaEnergyDetectThresholdDbm("
    )
    transmit_prepared = function_body(
        raw, "bool ZigbeeRadio::transmitPreparedPacket("
    )

    assert "radio_->TIFS = RADIO_TIFS_ResetValue;" in configure
    assert "radio_->TIFS = 40U" not in raw
    assert "TIFS cannot be made conditional on CRCOK" in configure
    assert "TIMER10/DPPIC10 schedules the" in configure
    assert re.search(r"kIeee802154AckTxRampStartUs\s*=\s*129U", raw)
    assert "radio_->EDCTRL = RADIO_EDCTRL_ResetValue;" in configure

    assert "RADIO_SHORTS_READY_EDSTART_Enabled" in energy
    assert "RADIO_SHORTS_EDEND_DISABLE_Enabled" in energy
    assert "RADIO_SHORTS_RXREADY_START" not in energy
    assert "TASKS_EDSTART" not in energy
    assert "TASKS_EDSTOP" in energy
    assert "EVENTS_EDEND" in energy
    assert "EVENTS_EDSTOPPED = 0U" in energy
    assert "radio_->EDSAMPLE & RADIO_EDSAMPLE_EDLVL_Msk" in energy
    assert "Returns the native RADIO EDSAMPLE value (0..127)" in header
    assert "min(4 * raw, 255)" in header

    conversion = function_body(
        openthread, "int8_t convertThreadEnergyScanToDbm(uint8_t edLevel)"
    )
    assert "kThreadEdRssiOffsetDbm" in conversion
    assert "static_cast<int16_t>(edLevel)" in conversion
    assert re.search(r"kThreadEdRssiOffsetDbm\s*=\s*-92", openthread)

    # nRF54L CCAEDTHRES is an offset from -92 dBm. CCA must transition from
    # RXIDLE to TX through hardware shorts, with CCABUSY disabling the radio;
    # a software-visible Disabled gap would permit another owner to interfere.
    assert re.search(r"kNrf54lRadioEdMinimumDbm\s*=\s*-92", raw)
    threshold_conversion = function_body(
        raw, "uint8_t ieee802154CcaEdThresholdRegister("
    )
    assert "dbm) -" in threshold_conversion
    assert "kNrf54lRadioEdMinimumDbm" in threshold_conversion
    assert "dbm < kNrf54lRadioEdMinimumDbm" in cca_setter
    assert "dbm > kNrf54lRadioEdMaximumDbm" in cca_setter
    staged_path = cca_setter[cca_setter.index("if (!rawRadioDisabled(radio_))") :]
    assert_order(staged_path, "ccaThresholdDbm_ = dbm;", "return true;")
    assert "cancelReceive" not in cca_setter
    assert "cancelBufferedReceive" not in cca_setter
    for short in (
        "RADIO_SHORTS_CCAIDLE_STOP_Enabled",
        "RADIO_SHORTS_CCAIDLE_TXEN_Enabled",
        "RADIO_SHORTS_CCABUSY_DISABLE_Enabled",
        "RADIO_SHORTS_TXREADY_START_Enabled",
        "RADIO_SHORTS_PHYEND_DISABLE_Enabled",
    ):
        assert short in transmit_prepared
    assert_order(
        transmit_prepared,
        "setCcaEnergyDetectThresholdDbm(ccaThresholdDbm_)",
        "radio_->SHORTS =",
        "radio_->TASKS_RXEN",
        "radio_->TASKS_CCASTART",
    )
    cca_to_tx = transmit_prepared[
        transmit_prepared.index("// PACKETPTR is already the TX buffer") :
    ]
    assert "radio_->TASKS_TXEN" not in cca_to_tx

    # Duration-based scans use the RADIO's fixed 128 us ED periods and EDCNT
    # (iterations minus one), then complete asynchronously from the platform
    # process loop. OpenThread's callback must not fire inline from the API.
    assert "(requestedUs + 127U) / 128U" in begin_energy
    assert "iterations - 1U" in begin_energy
    assert "RADIO_EDCTRL_EDCNT_Msk" in begin_energy
    assert "energyDetectArmed_ = true" in begin_energy
    assert "radio_->TASKS_RXEN" in begin_energy
    assert "radio_->EVENTS_EDEND == 0U" in poll_energy
    assert "return true;" in poll_energy
    assert_order(
        poll_energy,
        "radio_->EVENTS_EDEND == 0U",
        "waitRadioStateDisabledBudgeted",
        "radio_->EDCTRL = RADIO_EDCTRL_ResetValue",
        "releaseRadioConstantLatency()",
        "*outComplete = true",
    )
    ot_energy_api = function_body(openthread, "otError otPlatRadioEnergyScan(")
    ot_energy_service = function_body(
        openthread, "bool serviceThreadRadioEnergyScan()"
    )
    ot_energy_finish = function_body(
        openthread, "bool finishThreadRadioEnergyScan("
    )
    assert "beginEnergyDetectScan(scanDurationMs" in ot_energy_api
    assert "radioEnergyScanPending = true" in ot_energy_api
    assert "otPlatRadioEnergyScanDone" not in ot_energy_api
    assert "pollEnergyDetectScan" in ot_energy_service
    assert "if (!complete)" in ot_energy_service
    assert "otPlatRadioEnergyScanDone" in ot_energy_finish

    print("PASS TIFS, atomic CCA-to-TX, and duration-based EDSAMPLE contracts")


def main() -> None:
    test_hfxo_radio_readiness_and_anomaly_39()
    test_ble_background_clock_sequence_and_dppi_ownership()
    test_radio_errata_predicates_and_access()
    test_constant_latency_lifecycle()
    test_raw_radio_hfxo_ownership_lifecycle()
    test_tifs_units_and_energy_detect_sequence()
    print("PASS all Zigbee radio hardware contracts")


if __name__ == "__main__":
    main()
