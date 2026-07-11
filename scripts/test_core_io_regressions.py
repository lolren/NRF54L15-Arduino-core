#!/usr/bin/env python3
"""Run host utility sanitizers and validate nRF54 IRQ vector contracts."""

from __future__ import annotations

import os
import re
import runpy
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
TESTS = ROOT / "tests/core_io"

CHIPS = {
    "nrf54l15": {
        "last_irq": 269,
        "vectors": {
            28: "SWI00_IRQHandler",
            70: "AAR00_CCM00_IRQHandler",
            71: "ECB00_IRQHandler",
            74: "SPIM00_IRQHandler",
            133: "TIMER10_IRQHandler",
            135: "EGU10_IRQHandler",
            138: "RADIO_0_IRQHandler",
            139: "RADIO_1_IRQHandler",
            198: "SPIM20_IRQHandler",
            199: "SPIM21_IRQHandler",
            200: "SPIM22_IRQHandler",
            201: "EGU20_IRQHandler",
            202: "TIMER20_IRQHandler",
            203: "TIMER21_IRQHandler",
            204: "TIMER22_IRQHandler",
            205: "TIMER23_IRQHandler",
            206: "TIMER24_IRQHandler",
            208: "PDM20_IRQHandler",
            209: "PDM21_IRQHandler",
            210: "PWM20_IRQHandler",
            211: "PWM21_IRQHandler",
            212: "PWM22_IRQHandler",
            213: "SAADC_IRQHandler",
            214: "NFCT_IRQHandler",
            215: "TEMP_IRQHandler",
            218: "GPIOTE20_0_IRQHandler",
            219: "GPIOTE20_1_IRQHandler",
            221: "I2S20_IRQHandler",
            224: "QDEC20_IRQHandler",
            225: "QDEC21_IRQHandler",
            226: "GRTC_0_IRQHandler",
            227: "GRTC_1_IRQHandler",
            228: "GRTC_2_IRQHandler",
            229: "GRTC_3_IRQHandler",
            260: "SPIM30_IRQHandler",
            261: "CLOCK_POWER_IRQHandler",
            262: "LPCOMP_IRQHandler",
            264: "WDT30_IRQHandler",
            265: "WDT31_IRQHandler",
            268: "GPIOTE30_0_IRQHandler",
            269: "GPIOTE30_1_IRQHandler",
        },
        "irqs": {
            "SWI00": 28,
            "AAR00_CCM00": 70,
            "ECB00": 71,
            "SPIM00": 74,
            "TIMER10": 133,
            "SPIM20": 198,
            "SPIM21": 199,
            "SPIM22": 200,
            "SAADC": 213,
            "NFCT": 214,
            "GPIOTE20_0": 218,
            "GPIOTE20_1": 219,
            "I2S20": 221,
            "SPIM30": 260,
            "CLOCK_POWER": 261,
            "LPCOMP": 262,
            "GPIOTE30_0": 268,
            "GPIOTE30_1": 269,
        },
    },
    "nrf54lm20b": {
        "last_irq": 270,
        "vectors": {
            28: "SWI00_IRQHandler",
            74: "AAR00_CCM00_IRQHandler",
            75: "ECB00_IRQHandler",
            77: "SPIM00_IRQHandler",
            133: "TIMER10_IRQHandler",
            135: "EGU10_IRQHandler",
            138: "RADIO_0_IRQHandler",
            139: "RADIO_1_IRQHandler",
            198: "SPIM20_IRQHandler",
            199: "SPIM21_IRQHandler",
            200: "SPIM22_IRQHandler",
            201: "EGU20_IRQHandler",
            202: "TIMER20_IRQHandler",
            203: "TIMER21_IRQHandler",
            204: "TIMER22_IRQHandler",
            205: "TIMER23_IRQHandler",
            206: "TIMER24_IRQHandler",
            208: "PDM20_IRQHandler",
            209: "PDM21_IRQHandler",
            210: "PWM20_IRQHandler",
            211: "PWM21_IRQHandler",
            212: "PWM22_IRQHandler",
            213: "SAADC_IRQHandler",
            214: "NFCT_IRQHandler",
            215: "TEMP_IRQHandler",
            218: "GPIOTE20_0_IRQHandler",
            219: "GPIOTE20_1_IRQHandler",
            224: "QDEC20_IRQHandler",
            225: "QDEC21_IRQHandler",
            226: "GRTC_0_IRQHandler",
            227: "GRTC_1_IRQHandler",
            228: "GRTC_2_IRQHandler",
            229: "GRTC_3_IRQHandler",
            237: "SPIM23_IRQHandler",
            260: "SPIM30_IRQHandler",
            262: "LPCOMP_IRQHandler",
            264: "WDT30_IRQHandler",
            265: "WDT31_IRQHandler",
            268: "GPIOTE30_0_IRQHandler",
            269: "GPIOTE30_1_IRQHandler",
            270: "CLOCK_POWER_IRQHandler",
        },
        "irqs": {
            "SWI00": 28,
            "AAR00_CCM00": 74,
            "ECB00": 75,
            "SPIM00": 77,
            "TIMER10": 133,
            "SPIM20": 198,
            "SPIM21": 199,
            "SPIM22": 200,
            "SAADC": 213,
            "NFCT": 214,
            "GPIOTE20_0": 218,
            "GPIOTE20_1": 219,
            "SPIM23": 237,
            "SPIM30": 260,
            "LPCOMP": 262,
            "GPIOTE30_0": 268,
            "GPIOTE30_1": 269,
            "CLOCK_POWER": 270,
        },
    },
}


def run(command: list[str], *, env: dict[str, str] | None = None) -> None:
    subprocess.run(command, cwd=ROOT, env=env, check=True)


def vector_entries(path: Path) -> list[str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    label = next(i for i, line in enumerate(lines) if line.strip() == "g_pfnVectors:")
    size = next(
        i for i, line in enumerate(lines[label + 1 :], label + 1)
        if line.strip().startswith(".size g_pfnVectors")
    )
    entries: list[str] = []
    index = label + 1
    while index < size:
        code = lines[index].split("/*", 1)[0].strip()
        if code.startswith(".word"):
            entries.append(code.split()[1])
        elif code.startswith(".rept"):
            count = int(code.split()[1], 0)
            index += 1
            while index < size and not lines[index].strip().startswith(".word"):
                index += 1
            if index >= size:
                raise AssertionError(f"unterminated .rept in {path}")
            word = lines[index].split("/*", 1)[0].strip().split()[1]
            entries.extend([word] * count)
            while index < size and not lines[index].strip().startswith(".endr"):
                index += 1
            if index >= size:
                raise AssertionError(f"missing .endr in {path}")
        index += 1
    return entries


def validate_vectors() -> None:
    for chip, contract in CHIPS.items():
        core = PLATFORM / "cores" / chip
        startup = core / f"startup_{chip}.S"
        cmsis = core / "cmsis.h"
        entries = vector_entries(startup)
        expected_count = 16 + int(contract["last_irq"]) + 1
        assert len(entries) == expected_count, (
            f"{chip}: {len(entries)} vector entries, expected {expected_count}"
        )
        for irq, handler in contract["vectors"].items():
            actual = entries[16 + irq]
            assert actual == handler, f"{chip} IRQ {irq}: {actual}, expected {handler}"

        cmsis_text = cmsis.read_text(encoding="utf-8")
        for name, irq in contract["irqs"].items():
            match = re.search(rf"\b{name}_IRQn\s*=\s*(-?\d+)", cmsis_text)
            assert match is not None, f"{chip}: missing {name}_IRQn"
            assert int(match.group(1)) == irq, (
                f"{chip} {name}_IRQn={match.group(1)}, expected {irq}"
            )

        if chip == "nrf54lm20b":
            assert entries[16 + 221] == "Default_Handler"
            assert "I2S20_IRQn" not in cmsis_text
            assert all("I2S20" not in entry for entry in entries)
        print(f"PASS {chip} vectors: {len(entries)} entries")


def validate_cmsis_priority_contracts() -> None:
    for chip in CHIPS:
        cmsis_text = (PLATFORM / "cores" / chip / "cmsis.h").read_text(
            encoding="utf-8"
        )
        assert "__NVIC_SystemPriorityByte" in cmsis_text
        set_body = function_body(cmsis_text, "static inline void __NVIC_SetPriority(")
        get_body = function_body(cmsis_text, "static inline uint32_t __NVIC_GetPriority(")
        assert "0xE000ED18UL" in cmsis_text
        assert "__NVIC_SystemPriorityByte(IRQn)" in set_body
        assert "__NVIC_SystemPriorityByte(IRQn)" in get_body
        print(f"PASS {chip} CMSIS system-exception priority contract")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def validate_hardware_serial_contracts() -> None:
    for chip in CHIPS:
        source = (PLATFORM / "cores" / chip / "HardwareSerial.cpp").read_text(
            encoding="utf-8"
        )
        baud_body = function_body(source, "static uint32_t baud_to_reg(")
        assert "static_cast<uint64_t>(baud) << 32U" in baud_body
        assert "kPresets[bestIndex].baud) << 32U" not in baud_body

        end_body = function_body(source, "void HardwareSerial::end()")
        assert end_body.index("drainTxForShutdown()") < end_body.index(
            "U_TASKS_DMA_TX_STOP"
        )
        drain_body = function_body(source, "bool HardwareSerial::drainTxForShutdown()")
        assert "serial_byte_timeout_us" in drain_body
        assert "serviceTxDma()" in drain_body

        route_body = function_body(source, "static bool uart_route_valid(")
        assert "tx == 2U && rx == 0U" in route_body
        assert "tx == 8U && rx == 7U" in route_body
        nfc_release_body = function_body(
            source, "static void release_nfc_pads_for_gpio("
        )
        nfc_pad_body = function_body(source, "static bool is_nfc_pad(")
        expected_nfc_pins = (
            "pin == 2U || pin == 3U"
            if chip == "nrf54l15"
            else "pin == 1U || pin == 2U"
        )
        assert expected_nfc_pins in nfc_pad_body
        assert "is_nfc_pad(txPort, tx) || is_nfc_pad(rxPort, rx)" in nfc_release_body
        assert "NRF_NFCT->PADCONFIG" in nfc_release_body
        assert "NFCT_PADCONFIG_ENABLE_Disabled" in nfc_release_body
        assert source.count(
            "release_nfc_pads_for_gpio(txPort, tx, rxPort, rx);"
        ) == 2
        digital_source = (PLATFORM / "cores" / chip / "wiring_digital.c").read_text(
            encoding="utf-8"
        )
        digital_nfc_body = function_body(
            digital_source, "static void release_nfc_pad_for_gpio("
        )
        assert expected_nfc_pins.replace("pin", "d->pin") in digital_nfc_body
        assert "NRF_NFCT->PADCONFIG" in digital_nfc_body
        assert "NFCT_PADCONFIG_ENABLE_Disabled" in digital_nfc_body
        pin_mode_body = function_body(digital_source, "void pinMode(")
        assert "release_nfc_pad_for_gpio(&d);" in pin_mode_body
        interrupt_body = function_body(
            digital_source, "static void configure_pin_for_interrupt("
        )
        assert "release_nfc_pad_for_gpio(d);" in interrupt_body
        print(
            f"PASS {chip} serial shutdown, high-baud, route, and NFC-pad contracts"
        )


def validate_pca10156_serial_route_contracts() -> None:
    variant = (
        PLATFORM / "variants/nrf54l15dk_pca10156/pins_arduino.h"
    ).read_text(encoding="utf-8")
    assert "#define PIN_SERIAL_TX  PIN_P1_04" in variant
    assert "#define PIN_SERIAL_RX  PIN_P1_05" in variant
    assert "#define PIN_SERIAL1_TX PIN_P1_02" in variant
    assert "#define PIN_SERIAL1_RX PIN_P1_03" in variant
    assert "#define PIN_SERIAL1_TX PIN_SERIAL_TX" not in variant
    assert "#define PIN_SERIAL1_RX PIN_SERIAL_RX" not in variant

    serial_source = (PLATFORM / "cores/nrf54l15/HardwareSerial.cpp").read_text(
        encoding="utf-8"
    )
    assert "HardwareSerial Serial(NRF_UARTE20, PIN_SERIAL_TX, PIN_SERIAL_RX);" in serial_source
    assert "HardwareSerial Serial1(NRF_UARTE21, PIN_SERIAL1_TX, PIN_SERIAL1_RX);" in serial_source
    route_body = function_body(serial_source, "static bool uart_route_valid(")
    assert "if (txPort == 1U) {\n        return true;\n    }" in route_body
    print("PASS PCA10156 Serial and Serial1 default routes are independent")


def validate_thread_crypto_build_contracts() -> None:
    source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/matter_pbkdf2.cpp"
    ).read_text(encoding="utf-8")
    guard = source[: source.index('#include "matter_pbkdf2.h"')]
    assert "NRF54L15_CLEAN_MATTER_CORE_ENABLE" in guard
    assert "NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE" in guard
    assert "||" in guard
    print("PASS PBKDF2 implementation is available to Matter and Thread builds")


def validate_ble_disconnect_reason_contracts() -> None:
    source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_central_event.inc"
    ).read_text(encoding="utf-8")
    body = function_body(
        source, "bool BleRadio::pollCentralConnectionEvent(BleConnectionEvent* event,"
    )
    assert "bool terminateMicFailure = false;" in body
    assert body.count("terminateMicFailure = true;") == 4
    assert "terminateMicFailure\n            ? BleDisconnectReason::kMicFailure" in body
    assert "peerTerminateIndReceived ? BleDisconnectReason::kPeerTerminate" in body
    assert ": BleDisconnectReason::kInternalTerminate" in body
    assert "terminateMicFailure ? kBleLlErrorMicFailure : peerTerminateErrorCode" in body
    print("PASS BLE central MIC, peer, and internal disconnect classification")


def validate_custom_gatt_initial_value_capacity_contracts() -> None:
    source_root = (
        PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"
    )
    header = (source_root / "nrf54l15_hal.h").read_text(encoding="utf-8")
    implementation = (
        source_root / "nrf54l15_hal_parts/nrf54l15_hal_ble_custom_gatt.inc"
    ).read_text(encoding="utf-8")

    signature_pattern = re.compile(
        r"\bbool\s+(?:BleRadio::)?"
        r"(addCustomGattCharacteristic[A-Za-z0-9_]*)"
        r"\s*\(([^;{}]*)\)\s*(?:;|\{)",
        re.DOTALL,
    )
    expected_header = {
        "addCustomGattCharacteristic": 2,
        "addCustomGattCharacteristicWithDescriptors": 1,
        "addCustomGattCharacteristic128": 2,
        "addCustomGattCharacteristic128WithDescriptors": 1,
        "addCustomGattCharacteristicCommon": 1,
        "addCustomGattCharacteristicUuid": 1,
    }
    expected_implementation = expected_header | {
        "addCustomGattCharacteristicUuid": 0,
    }

    for label, source, expected in (
        ("header", header, expected_header),
        ("implementation", implementation, expected_implementation),
    ):
        signatures = signature_pattern.findall(source)
        actual = {
            name: sum(1 for signature_name, _ in signatures if signature_name == name)
            for name in expected
        }
        assert actual == expected, f"custom GATT {label} signatures changed: {actual}"
        assert len(signatures) == sum(expected.values())
        for name, parameters in signatures:
            normalized = " ".join(parameters.split())
            assert "uint16_t initialValueLength" in normalized, (
                f"{label} {name} narrows the initial value length: {normalized}"
            )
            assert "uint8_t initialValueLength" not in normalized

    max_length_match = re.search(
        r"static constexpr uint16_t\s+kCustomGattMaxValueLength\s*=\s*(\d+)U;",
        header,
    )
    assert max_length_match is not None
    max_length = int(max_length_match.group(1))
    assert max_length == 512
    assert max_length > 0xFF

    common_body = function_body(
        implementation, "bool BleRadio::addCustomGattCharacteristicCommon("
    )
    assert "initialValueLength > kCustomGattMaxValueLength" in common_body
    assert "characteristic.valueLength = initialValueLength;" in common_body
    assert "memcpy(characteristic.value, initialValue, initialValueLength);" in common_body
    assert "uint16_t valueLength;" in header
    assert "uint8_t value[kCustomGattMaxValueLength];" in header
    enqueue_signature = re.search(
        r"enqueueCustomGattNotification\s*\([^;]*uint16_t\s+valueLength\s*\);",
        header,
        re.DOTALL,
    )
    assert enqueue_signature is not None
    enqueue_body = function_body(
        implementation, "bool BleRadio::enqueueCustomGattNotification("
    )
    capacity_guard = "valueLength > maxNotificationValueLength()"
    narrowing_store = "slot.valueLength = static_cast<uint8_t>(valueLength);"
    assert capacity_guard in enqueue_body
    assert narrowing_store in enqueue_body
    assert enqueue_body.index(capacity_guard) < enqueue_body.index(narrowing_store)
    bluefruit = (
        PLATFORM / "libraries/Bluefruit52Lib/src/bluefruit.cpp"
    ).read_text(encoding="utf-8")
    bluefruit_begin = function_body(bluefruit, "err_t BLECharacteristic::begin()")
    assert "const uint16_t initialLen = clampValueLen(_value_len);" in bluefruit_begin
    assert "0xFFU" not in bluefruit_begin
    print(
        "PASS custom GATT stores 512-byte values and rejects oversized "
        "single-PDU notifications before narrowing"
    )


def validate_parser_output_validity_contracts() -> None:
    source_root = PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"
    zigbee_stack = (source_root / "zigbee_stack.cpp").read_text(encoding="utf-8")
    zigbee_security = (source_root / "zigbee_security.cpp").read_text(
        encoding="utf-8"
    )
    vpr = (source_root / "nrf54l15_vpr.cpp").read_text(encoding="utf-8")

    simple_parsers = (
        (
            zigbee_stack,
            "bool ZigbeeCodec::parseMacFrame(",
            "*outFrame = ZigbeeMacFrame{};",
            "outFrame->valid = true;",
        ),
        (
            zigbee_stack,
            "bool ZigbeeCodec::parseNwkFrame(",
            "*outFrame = ZigbeeNetworkFrame{};",
            "outFrame->valid = true;",
        ),
        (
            zigbee_stack,
            "bool ZigbeeCodec::parseApsDataFrame(",
            "*outFrame = ZigbeeApsDataFrame{};",
            "outFrame->valid = true;",
        ),
        (
            zigbee_stack,
            "bool ZigbeeCodec::parseApsAcknowledgementFrame(",
            "*outFrame = ZigbeeApsAcknowledgementFrame{};",
            "outFrame->valid = true;",
        ),
        (
            zigbee_stack,
            "bool ZigbeeCodec::parseApsTransportKeyCommand(",
            "*outKey = ZigbeeApsTransportKey{};",
            "outKey->valid = true;",
        ),
        (
            zigbee_security,
            "bool ZigbeeSecurity::parseNwkSecurityHeader(",
            "*outHeader = ZigbeeNwkSecurityHeader{};",
            "outHeader->valid = true;",
        ),
        (
            zigbee_security,
            "bool ZigbeeSecurity::parseApsSecurityHeader(",
            "*outHeader = ZigbeeApsSecurityHeader{};",
            "outHeader->valid = true;",
        ),
    )
    for source, signature, reset, valid_commit in simple_parsers:
        body = function_body(source, signature)
        assert reset in body, f"{signature} does not restore typed defaults"
        assert body.count(valid_commit) == 1
        assert body.rindex(valid_commit) > body.rfind("return false;")

    beacon_body = function_body(
        zigbee_stack, "bool ZigbeeCodec::parseBeaconFrame("
    )
    assert "*outView = ZigbeeMacBeaconView{};" in beacon_body
    assert (
        "outView->network.panCoordinator = outView->panCoordinator;"
        in beacon_body
    )
    assert (
        "outView->network.associationPermit = outView->associationPermit;"
        in beacon_body
    )

    secured_parsers = (
        (
            "bool ZigbeeSecurity::parseSecuredNwkFrame(",
            "ZigbeeNetworkFrame parsedFrame{};",
            "ZigbeeNwkSecurityHeader parsedSecurity{};",
        ),
        (
            "bool ZigbeeSecurity::parseSecuredApsCommandFrame(",
            "ZigbeeApsCommandFrame parsedFrame{};",
            "ZigbeeApsSecurityHeader parsedSecurity{};",
        ),
    )
    for signature, frame_candidate, security_candidate in secured_parsers:
        body = function_body(zigbee_security, signature)
        assert frame_candidate in body
        assert security_candidate in body
        decrypt = body.index("decryptCcmStar(")
        last_failure = body.rfind("return false;")
        assert body.rindex("parsedFrame.valid = true;") > max(decrypt, last_failure)
        assert body.rindex("*outFrame = parsedFrame;") > max(decrypt, last_failure)
        assert body.rindex("*outSecurity = parsedSecurity;") > max(
            decrypt, last_failure
        )
        assert "outFrame->valid = true;" not in body

    transport_body = function_body(
        zigbee_security,
        "bool ZigbeeSecurity::parseSecuredApsTransportKeyCommand(",
    )
    assert "ZigbeeApsSecurityHeader parsedSecurity{};" in transport_body
    assert "&parsedSecurity," in transport_body
    assert transport_body.rindex("outTransportKey->valid = true;") > (
        transport_body.rfind("return false;")
    )
    assert transport_body.rindex("*outSecurity = parsedSecurity;") > (
        transport_body.rfind("return false;")
    )

    for signature in (
        "bool ZigbeeSecurity::parseSecuredApsUpdateDeviceCommand(",
        "bool ZigbeeSecurity::parseSecuredApsSwitchKeyCommand(",
    ):
        body = function_body(zigbee_security, signature)
        assert "*outSecurity = ZigbeeApsSecurityHeader{};" in body
        assert "ZigbeeApsSecurityHeader parsedSecurity{};" in body
        assert "&parsedSecurity," in body
        assert body.rindex("*outSecurity = parsedSecurity;") > body.rfind(
            "return false;"
        )

    vpr_body = function_body(
        vpr,
        "bool VprControllerServiceHost::readBleCsWorkflowCompletedResult(",
    )
    assert "*result = VprBleCsCompletedResultPayload{};" in vpr_body
    assert vpr_body.index("parsed.valid = payload[2] != 0U;") > vpr_body.index(
        "parsed.payloadLen > VprBleCsCompletedResultPayload::kMaxPayloadBytes"
    )
    assert vpr_body.rindex("*result = parsed;") > vpr_body.rfind("return false;")
    print("PASS malformed parser outputs remain invalid until fully accepted")


def validate_zigbee_persistence_reset_contracts() -> None:
    source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/zigbee_persistence.cpp"
    ).read_text(encoding="utf-8")
    assert "std::is_trivially_copyable<ZigbeePersistentState>::value" in source
    reset_body = function_body(source, "void resetPersistentState(")
    assert "memset(static_cast<void*>(state), 0, sizeof(*state));" in reset_body
    assert "reporting = ZigbeeReportingConfiguration{};" in reset_body
    assert "binding = ZigbeeBindingEntry{};" in reset_body

    load_body = function_body(source, "bool loadChunkedState(")
    initialize_body = function_body(
        source, "void ZigbeePersistentStateStore::initialize("
    )
    assert "resetPersistentState(outState);" in load_body
    assert "resetPersistentState(state);" in initialize_body
    print("PASS Zigbee persisted state keeps deterministic padding and typed defaults")


def validate_protocol_typed_reset_contracts() -> None:
    source_root = PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"

    pase_header = (source_root / "matter_pase_commissioning.h").read_text(
        encoding="utf-8"
    )
    pase_source = (source_root / "matter_pase_commissioning.cpp").read_text(
        encoding="utf-8"
    )
    assert "kMatterSpake2pPbkdf2Iterations = 2000U" in pase_header
    assert "uint32_t iterations = kMatterSpake2pPbkdf2Iterations;" in pase_header
    pase_end = function_body(pase_source, "void MatterPaseCommissioning::end()")
    assert "session_ = MatterPaseSessionState{};" in pase_end
    assert "verifier_ = MatterSpake2pVerifier{};" in pase_end
    assert "memset" not in pase_end

    credentials = (source_root / "matter_credentials.cpp").read_text(
        encoding="utf-8"
    )
    credential_reset = function_body(
        credentials, "void resetPersistentCredentialsState("
    )
    assert "bytes[i] = 0U;" in credential_reset
    assert "copyPersistentCredentialsMembers(state, defaults);" in credential_reset
    credential_save = function_body(
        credentials, "bool MatterCredentials::saveToStorage("
    )
    assert "resetPersistentCredentialsState(&canonicalState);" in credential_save
    assert "copyPersistentCredentialsMembers(&canonicalState, state);" in credential_save
    assert "canonicalState = state;" not in credential_save
    assert "prefs.putBytes(kCredentialsKey, &canonicalState" in credential_save

    openthread = (source_root / "openthread_platform_nrf54l15.cpp").read_text(
        encoding="utf-8"
    )
    ot_init = function_body(openthread, "void otSysInit(int, char**)")
    assert "state.snapshot = OpenThreadPlatformSkeletonSnapshot{};" in ot_init
    assert re.search(
        r"void\s+makeDataChunkKey\s*\(\s*uint16_t\s+key,\s*"
        r"uint16_t\s+index,",
        openthread,
    )
    chunk_key = function_body(openthread, "void makeDataChunkKey(")
    assert '"k%04X.%02u.d%02u"' in chunk_key
    assert "static_cast<unsigned>(index)" in chunk_key
    settings_get = function_body(openthread, "otError otPlatSettingsGet(")
    assert "static_cast<uint16_t>(index)" in settings_get

    channel_sounding = (source_root / "ble_channel_sounding.cpp").read_text(
        encoding="utf-8"
    )
    sweep = function_body(
        channel_sounding,
        "bool BleCsConnectedMode2SweepRunner::runInitiator(",
    )
    assert "measurements[i] = BleCsChannelMeasurement{};" in sweep
    print("PASS Matter, OpenThread, and channel-sounding typed sentinel resets")


def validate_channel_sounding_public_examples_contracts() -> None:
    examples = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding"
    )
    expected = ["BleChannelSoundingInitiator", "BleChannelSoundingReflector"]
    actual = sorted(path.name for path in examples.iterdir() if path.is_dir())
    assert actual == expected, (
        "public Channel Sounding examples must contain only the canonical pair: "
        f"{actual}"
    )
    for name in expected:
        sketch_path = examples / name / f"{name}.ino"
        assert sketch_path.is_file()
        sketch = sketch_path.read_text(encoding="utf-8")
        assert '#include "ble_cs_controller_runtime.h"' in sketch
        assert "BleCsControllerRuntime" in sketch
        assert "BleCsSubeventResultReassembler" in sketch
        assert "kBleCsHciEvtSubeventResult" in sketch
        assert "kBleCsHciEvtSubeventResultContinue" in sketch
        assert "measureChannelAveraged(" not in sketch
        assert "listenAndReflectOnce(" not in sketch
        assert "protocol=physical_pbr" not in sketch

    tracked = subprocess.run(
        ["git", "ls-files", "--", "*.uf2"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if tracked.returncode == 0:
        tracked_uf2 = [
            path
            for line in tracked.stdout.splitlines()
            if (path := ROOT / line).is_file()
        ]
    else:
        tracked_uf2 = list(ROOT.rglob("*.uf2"))
    assert not tracked_uf2, f"tracked UF2 artifacts are not allowed: {tracked_uf2}"
    public_uf2 = list(examples.rglob("*.uf2"))
    assert not public_uf2, (
        f"UF2 build artifacts are not allowed in public CS examples: {public_uf2}"
    )
    print(
        "PASS exactly two controller-backed public Channel Sounding examples "
        "and no UF2 artifacts"
    )


def validate_spi_contracts() -> None:
    source = (PLATFORM / "cores/nrf54lm20b/SPI.cpp").read_text(encoding="utf-8")
    begin_transaction = function_body(
        source, "void SPIClass::beginTransaction(SPISettings settings)"
    )
    assert "if (!_initialized || _spim == nullptr)" in begin_transaction
    assert begin_transaction.index("if (!_initialized || _spim == nullptr)") < (
        begin_transaction.index("applySettings()")
    )
    assert "_inTransaction = false;" in begin_transaction
    print("PASS nrf54lm20b SPI beginTransaction route-failure guard")


def validate_system_off_wake_contracts() -> None:
    for chip in CHIPS:
        source = (PLATFORM / "cores" / chip / "wiring_time.c").read_text(
            encoding="utf-8"
        )
        arm_body = function_body(
            source, "static void armSystemOffWakeCompare("
        )
        wake_selector_body = function_body(
            source, "static uint8_t systemOffWakeChannel("
        )
        systemoff_entry_body = function_body(
            source, "static void enterSystemOffInternal(bool disableRamRetention"
        )
        program_body = function_body(
            source, "static system_off_wake_program_status_t programSystemOffWakeUs("
        )
        assert "uint64_t wakeTimestampUs" in source
        assert "CCADD" not in arm_body
        assert "GRTC_CC_CCEN_ACTIVE_Enable" in arm_body
        assert "kSystemOffWakePreferredCcChannel = 6U" in source
        assert "highestSetBit(available)" not in wake_selector_body
        assert "lowestSetBit(available)" in wake_selector_body
        assert wake_selector_body.index("kSystemOffWakePreferredCcChannel") < (
            wake_selector_body.index("lowestSetBit(available)")
        )
        assert "readGrtcCounterUs(grtc) + (uint64_t)wakeDelayUs" in program_body
        assert program_body.index("readGrtcCounterUs(grtc) + (uint64_t)wakeDelayUs") < (
            program_body.index("armSystemOffWakeCompare(")
        )
        assert "kScbScrSleepDeep_Msk" in systemoff_entry_body
        assert "__asm volatile(\"wfi\")" in systemoff_entry_body
        assert "timedWake && anyGrtcCompareEvent(NRF_GRTC)" in systemoff_entry_body
        assert "abortSystemOffWithReset();" in systemoff_entry_body
        assert "__asm volatile(\"wfe\")" not in systemoff_entry_body
        wfi_index = systemoff_entry_body.index("__asm volatile(\"wfi\")")
        fallback_index = systemoff_entry_body.index(
            "timedWake && anyGrtcCompareEvent(NRF_GRTC)", wfi_index
        )
        assert systemoff_entry_body.index("kScbScrSleepDeep_Msk") < (
            systemoff_entry_body.index("NRF_REGULATORS->SYSTEMOFF")
        )
        assert wfi_index < fallback_index
        assert fallback_index < (
            systemoff_entry_body.index("abortSystemOffWithReset();", fallback_index)
        )
        print(f"PASS {chip} timed SYSTEMOFF absolute GRTC compare/channel contract")

    hal_source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_timebase.cpp"
    ).read_text(encoding="utf-8")
    hal_arm_body = function_body(hal_source, "void armSystemOffWakeCompare(")
    assert "GRTC_CC_CCEN_ACTIVE_Enable" in hal_arm_body
    print("PASS HAL timed SYSTEMOFF compare explicit enable contract")


def validate_xiao_low_power_board_contracts() -> None:
    source = (PLATFORM / "variants/xiao_nrf54l15/variant.cpp").read_text(
        encoding="utf-8"
    )
    board_state_body = function_body(
        source, 'extern "C" void xiaoNrf54l15EnterLowestPowerBoardState('
    )
    assert "gpioSetInputHighZ(kSamd11TxPort" not in board_state_body
    assert "gpioSetInputHighZ(kSamd11RxPort" not in board_state_body
    print("PASS XIAO low-power board state preserves SAMD11 serial bridge pins")

    lm20_time = (PLATFORM / "cores/nrf54lm20b/wiring_time.c").read_text(
        encoding="utf-8"
    )
    lm20_clock_select = function_body(
        lm20_time, "static uint32_t selectRunningGrtcLfClockSource("
    )
    assert "ARDUINO_NRF54LM20A" in lm20_clock_select
    assert "startLfclkSource(CLOCK_LFCLK_SRC_SRC_LFXO);" in lm20_clock_select
    assert "return GRTC_CLKCFG_CLKSEL_SystemLFCLK;" in lm20_clock_select
    assert "ensureSystemOffLfxoRunning" not in lm20_clock_select
    assert "CLOCK_LFCLK_SRC_SRC_LFRC" in lm20_clock_select

    lm20_system = (PLATFORM / "cores/nrf54lm20b/system_nrf54lm20b.c").read_text(
        encoding="utf-8"
    )
    assert "lfxoIntcapFemtoF = 17000UL" in lm20_system
    assert "hfxoIntcapFemtoF = 15000UL" in lm20_system

    qspi_transport = (
        PLATFORM
        / "libraries/Adafruit_SPIFlash/src/qspi/Adafruit_FlashTransport_QSPI_NRF54.cpp"
    ).read_text(encoding="utf-8")
    qspi_end = function_body(
        qspi_transport, "void Adafruit_FlashTransport_QSPI_NRF54::end()"
    )
    assert "XiaoQspiFlash.prepareForSleep()" in qspi_end
    assert "XiaoQspiFlash.end()" not in qspi_end

    npm_source = (
        PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src/npm1300.cpp"
    ).read_text(encoding="utf-8")
    npm_sleep = function_body(npm_source, "bool npm1300_prepare_for_sleep(")
    assert "kAdcOffsetIbatEnable, 0U" in npm_sleep
    npm_bus_end = function_body(npm_source, "void pmic_bus_end()")
    assert "GPIO_PIN_CNF_INPUT_Disconnect" in npm_bus_end
    system_off_prepare = function_body(
        lm20_time, "bool nrf54lm20b_core_prepare_system_off(void)\n{"
    )
    assert "xiaoNrf54lm20PmicPrepareForSleep" in system_off_prepare
    assert "npm1300_prepare_for_sleep" not in system_off_prepare

    lm20_variant = (
        PLATFORM / "variants/xiao_nrf54lm20b/variant.cpp"
    ).read_text(encoding="utf-8")
    variant_pmic_sleep = function_body(
        lm20_variant, 'extern "C" int xiaoNrf54lm20PmicPrepareForSleep(void)'
    )
    assert "kPmicAdcBase = 0x05U" in lm20_variant
    assert "kPmicIbatEnableOffset" in variant_pmic_sleep
    assert "parkPmicPins" in variant_pmic_sleep

    delay_probe_paths = [
        PLATFORM / "examples/Power/DelayAutoLowPowerMeasure/DelayAutoLowPowerMeasure.ino",
        PLATFORM / "examples/XiaoL15/DelayAutoLowPowerMeasure/DelayAutoLowPowerMeasure.ino",
    ]
    delay_probes = [path.read_text(encoding="utf-8") for path in delay_probe_paths]
    assert delay_probes[0] == delay_probes[1], "delay-current probe copies diverged"
    assert "npm1300_imu_mic_power_enable(false)" in delay_probes[0]
    assert "npm1300_buck1_set_mode(NPM1300_BUCK_MODE_AUTO)" in delay_probes[0]
    assert "npm1300_prepare_for_sleep()" in delay_probes[0]

    system_off_probe = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/examples/LowPower/LowPowerZephyrParityBlink/LowPowerZephyrParityBlink.ino"
    ).read_text(encoding="utf-8")
    assert "npm1300_imu_mic_power_enable(false)" in system_off_probe
    assert "npm1300_buck1_set_mode(NPM1300_BUCK_MODE_FORCE_HYST)" in system_off_probe
    assert "npm1300_prepare_for_sleep()" in system_off_probe
    print("PASS LM20A LFXO, oscillator-load, QSPI, and PMIC sleep contracts")


def validate_xiao_retention_probe_contracts() -> None:
    probe_paths = [
        PLATFORM / "examples/Power/SenseDelayRailRetentionProbe/SenseDelayRailRetentionProbe.ino",
        PLATFORM / "examples/XiaoL15/SenseDelayRailRetentionProbe/SenseDelayRailRetentionProbe.ino",
    ]
    probes = [path.read_text(encoding="utf-8") for path in probe_paths]
    assert probes[0] == probes[1], "retention probe copies diverged"
    probe = probes[0]
    assert "kProbeCompareChannel = 5U" in probe
    assert "BLE Support: Disabled (required)" in probe
    assert "#error \"SenseDelayRailRetentionProbe requires Tools -> BLE Support -> Disabled.\"" in probe
    setup_body = function_body(probe, "void setup()")
    assert setup_body.index("delay(10);") < setup_body.index(
        "runRetentionMeasurement();"
    )
    assert "g_grtc.begin(" not in probe
    assert probe.index("runRetentionMeasurement();") < probe.index("Serial.begin(115200);")
    assert "retention_status=" in probe
    assert "g_postRfCtlPin == 0U" in probe
    assert "g_vbatRaw > 0" in probe

    matrix_namespace = runpy.run_path(str(ROOT / "scripts/build_all_examples.py"))
    feature_options = matrix_namespace["feature_options"]
    for path in probe_paths:
        relative = path.relative_to(PLATFORM).as_posix()
        assert "clean_ble=off" in feature_options(relative)

    hooks_source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_hooks.inc"
    ).read_text(encoding="utf-8")
    hal_source = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp"
    ).read_text(encoding="utf-8")
    mask_body = function_body(
        hooks_source, "extern \"C\" uint32_t nrf54l15_ble_grtc_reserved_cc_mask(void)"
    )
    irq_body = function_body(
        hal_source, "extern \"C\" void nrf54l15_ble_grtc_irq_service(void)"
    )
    assert "NRF54L15_CLEAN_BLE_DISABLED" in mask_body
    assert "return 0U;" in mask_body
    assert "NRF54L15_CLEAN_BLE_DISABLED" in irq_body
    assert irq_body.index("nrf54l15_grtc_irq_observer();") > irq_body.index("#endif")
    print("PASS BLE-disabled GRTC ownership and XIAO rail-retention probe contract")


def validate_serial_fabric_runtime_probe_contracts() -> None:
    probe_paths = [
        PLATFORM / "examples/Serial/SerialFabricRuntimeProbe/SerialFabricRuntimeProbe.ino",
        PLATFORM / "examples/Peripherals/SerialFabricRuntimeProbe/SerialFabricRuntimeProbe.ino",
    ]
    probes = [path.read_text(encoding="utf-8") for path in probe_paths]
    assert probes[0] == probes[1], "serial-fabric probe copies diverged"
    probe = probes[0]
    assert "void announceStage(const char* stage)" in probe
    assert "Serial.flush();" in probe
    assert "g_lastStatusMs" in probe
    assert "(now - g_lastStatusMs) >= 1000UL" in probe
    for stage in ("uart22", "uart30", "twim22", "twim30", "spim22", "spim30"):
        assert f'announceStage("{stage}")' in probe
    print("PASS serial-fabric runtime probe duplicate and CLI-status contract")


def validate_ble_security_hardening_contracts() -> None:
    parts = (
        PLATFORM
        / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts"
    )
    crypto_service = (parts / "nrf54l15_hal_internal_crypto_service.inc").read_text(
        encoding="utf-8"
    )
    random_fill = function_body(
        crypto_service, "bool fillBleSecurityRandomBytes("
    )
    assert "rng.fill(out, len, spinLimit)" in random_fill
    assert "memset(out, 0, len);" in random_fill
    assert "fillPseudoRandomBytes" not in random_fill

    crypto_hal = (parts / "nrf54l15_hal_crypto_analog.inc").read_text(
        encoding="utf-8"
    )
    rng_begin = function_body(crypto_hal, "bool CracenRng::beginNonBlocking()")
    assert "tryAcquireCracenRng()" in rng_begin
    assert "CRACENCORE_RNGCONTROL_CONTROL_SOFTRST_Msk" in rng_begin
    assert "RNGCONTROL.WARMUPPERIOD = 512U" in rng_begin
    assert "RNGCONTROL.SAMPLINGPERIOD = 1U" in rng_begin
    assert "RNGCONTROL.INITWAITVAL = 512U" in rng_begin
    assert "NB128BITBLOCKS" in rng_begin
    rng_conditioning = function_body(
        crypto_hal, "bool CracenRng::setupConditioningKeyIfAvailable()"
    )
    assert "availableWords() < 4U" in rng_conditioning
    assert "RNGCONTROL.KEY[i] = core_->RNGCONTROL.FIFO[0]" in rng_conditioning
    rng_end = function_body(crypto_hal, "void CracenRng::end()")
    assert "if (!active_)" in rng_end
    assert "releaseCracenRng();" in rng_end

    ll_security = (parts / "nrf54l15_hal_ble_ll_security.inc").read_text(
        encoding="utf-8"
    )
    smp_clear = function_body(ll_security, "void BleRadio::clearSmpSecurityState()")
    assert "clearSmpPairingState();" in smp_clear
    assert "clearEncryptionState();" not in smp_clear
    connection_clear = function_body(
        ll_security, "void BleRadio::clearConnectionSecurityState()"
    )
    assert "clearSmpSecurityState();" in connection_clear
    assert "clearEncryptionState();" in connection_clear
    timeout = function_body(ll_security, "void BleRadio::serviceSmpTimer()")
    assert "connectionPendingTxSmpFragment_" in timeout
    assert "connectionTxHistoryValid_ = false" not in timeout
    key_distribution = function_body(
        ll_security, "bool BleRadio::completeSmpKeyDistributionIfDone()"
    )
    assert "smpLocalIdAddressAcked_" in key_distribution
    identity_ack = function_body(
        ll_security, "void BleRadio::noteLastSmpTxAcknowledged()"
    )
    assert "kSmpCodeIdentityAddressInformation" in identity_ack
    prefetch = function_body(
        ll_security, "void BleRadio::prefetchConnectionSecurityMaterial("
    )
    assert "uint8_t material[28]" in prefetch
    assert prefetch.count("fillBleSecurityRandomBytes(") == 1
    bond_build = function_body(
        ll_security, "bool BleRadio::buildCurrentBondRecord("
    )
    assert "kBleBondRecordFlagAuthenticated" in bond_build
    assert "kBleBondRecordFlagSecureConnections" in bond_build

    smp = (parts / "nrf54l15_hal_ble_att_l2cap.inc").read_text(encoding="utf-8")
    assert "SMP_SECURITY_REQUEST_BOND_UPGRADE" in smp
    assert "clearSmpPairingState();" in smp
    assert "kBleBondRecordFlagSecureConnections" in smp
    assert "SMP_RESERVED_CODE_IGNORED" in smp
    print("PASS BLE SMP, bond-upgrade, timeout, and fail-closed RNG contracts")


def compile_and_run_host_tests(temp: Path) -> None:
    cxx = os.environ.get("CXX")
    if not cxx:
        cxx = "/usr/bin/g++" if Path("/usr/bin/g++").is_file() else "g++"
    if shutil.which(cxx) is None:
        raise SystemExit(f"C++ compiler not found: {cxx}")
    core = PLATFORM / "cores/nrf54l15"
    common = [cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror"]
    sanitizer_env = os.environ.copy()
    sanitizer_env["ASAN_OPTIONS"] = "abort_on_error=1:detect_leaks=1"
    sanitizer_env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"

    wstring = temp / "wstring_alias_test"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        f"-I{core}",
        str(TESTS / "wstring_alias_test.cpp"),
        "-o", str(wstring),
    ])
    run([str(wstring)], env=sanitizer_env)
    print("PASS WString alias/self-concat ASan+UBSan")

    timer = temp / "software_timer_test"
    run(common + [
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-include", str(TESTS / "software_timer_stubs.h"),
        f"-I{core}",
        str(core / "SoftwareTimer.cpp"),
        str(TESTS / "software_timer_test.cpp"),
        "-o", str(timer),
    ])
    run([str(timer)], env=sanitizer_env)
    print("PASS SoftwareTimer deletion/lifetime ASan+UBSan")

    math_test = temp / "wiring_math_test"
    run(common + [
        "-fsanitize=undefined",
        "-fno-sanitize-recover=undefined",
        f"-I{core}",
        str(TESTS / "wiring_math_test.cpp"),
        "-o", str(math_test),
    ])
    run([str(math_test)], env=sanitizer_env)
    print("PASS map extreme-range UBSan")

    for chip, contract in CHIPS.items():
        nvic_test = temp / f"nvic_layout_{chip}"
        run(common + [
            "-Wno-int-to-pointer-cast",
            f"-DEXPECTED_LAST_IRQ={contract['last_irq']}",
            f"-I{PLATFORM / 'cores' / chip}",
            str(TESTS / "nvic_layout_test.cpp"),
            "-o", str(nvic_test),
        ])
        run([str(nvic_test)])
        print(f"PASS {chip} NVIC register layout and IRQ priority coverage")


def main() -> int:
    validate_vectors()
    validate_cmsis_priority_contracts()
    validate_hardware_serial_contracts()
    validate_pca10156_serial_route_contracts()
    validate_thread_crypto_build_contracts()
    validate_ble_disconnect_reason_contracts()
    validate_custom_gatt_initial_value_capacity_contracts()
    validate_parser_output_validity_contracts()
    validate_zigbee_persistence_reset_contracts()
    validate_protocol_typed_reset_contracts()
    validate_channel_sounding_public_examples_contracts()
    validate_spi_contracts()
    validate_system_off_wake_contracts()
    validate_xiao_low_power_board_contracts()
    validate_xiao_retention_probe_contracts()
    validate_serial_fabric_runtime_probe_contracts()
    validate_ble_security_hardening_contracts()
    with tempfile.TemporaryDirectory(prefix="nrf54-core-io-") as directory:
        compile_and_run_host_tests(Path(directory))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
