#!/usr/bin/env python3
"""Validate legacy directed-advertising controller and Bluefruit contracts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
HAL = PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"
BLUEFRUIT = PLATFORM / "libraries/Bluefruit52Lib/src"


def source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_body(text: str, signature: str, next_signature: str) -> str:
    start = text.index(signature)
    end = text.index(next_signature, start)
    return text[start:end]


def validate_bluefruit_public_contract() -> None:
    common = source(BLUEFRUIT / "bluefruit_common.h")
    header = source(BLUEFRUIT / "bluefruit.h")
    implementation = source(BLUEFRUIT / "bluefruit.cpp")
    example = source(
        BLUEFRUIT.parent
        / "examples/Advertising/directed_advertising/directed_advertising.ino"
    )

    canonical_types = (
        "#define BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED 0x01U",
        "#define BLE_GAP_ADV_TYPE_CONNECTABLE_NONSCANNABLE_DIRECTED_HIGH_DUTY_CYCLE 0x02U",
        "#define BLE_GAP_ADV_TYPE_CONNECTABLE_NONSCANNABLE_DIRECTED 0x03U",
        "#define BLE_GAP_ADV_TYPE_NONCONNECTABLE_SCANNABLE_UNDIRECTED 0x04U",
        "#define BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED 0x05U",
    )
    for contract in canonical_types:
        assert contract in common
    assert "#define BLE_GAP_ADV_TYPE_ADV_NONCONN_IND" in common
    assert "BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED" in common
    assert "ble_gap_addr_t direct_addr;" in common
    assert "void setPeerAddress(const ble_gap_addr_t& peer_addr);" in header
    assert "ble_gap_addr_t peer_addr_;" in header
    assert "bool peer_addr_set_;" in header

    mapping = function_body(
        implementation,
        "bool bluefruitAdvertisingTypeToHal(",
        "unsigned long schedulerTimeMs()",
    )
    assert (
        "case BLE_GAP_ADV_TYPE_CONNECTABLE_NONSCANNABLE_DIRECTED_HIGH_DUTY_CYCLE:"
        in mapping
    )
    assert "case BLE_GAP_ADV_TYPE_CONNECTABLE_NONSCANNABLE_DIRECTED:" in mapping
    assert "*outType = xiao_nrf54l15::BleAdvPduType::kAdvDirectInd;" in mapping
    assert "case BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED:" in mapping
    assert "*outType = xiao_nrf54l15::BleAdvPduType::kAdvNonConnInd;" in mapping

    start = function_body(
        implementation,
        "bool BLEAdvertising::start(uint16_t timeout)",
        "bool BLEAdvertising::stop()",
    )
    for contract in (
        "!peer_addr_set_",
        "!bluefruitGapAddressValid(peer_addr_)",
        "len_ != 0U",
        "Bluefruit.ScanResponse.count() != 0U",
    ):
        assert contract in start

    assert "kDirectedHighDutyMaxIntervalUnits = 6U" in implementation
    assert "kDirectedHighDutyMaxDurationUs = 1280000ULL" in implementation
    assert "directed_high_duty_deadline_us_" in implementation
    assert "bluefruitAdvertisingTypeIsHighDutyDirected(advType)\n            ? 0U" in implementation
    for contract in (
        "Bluefruit.Security.getBondPeerIdentityAddress(&target)",
        "Bluefruit.Security.getBondPeerAddress(&target)",
        "Bluefruit.Advertising.clearData();",
        "Bluefruit.ScanResponse.clearData();",
        "Bluefruit.Advertising.setPeerAddress(target);",
        "BLE_GAP_ADV_TYPE_CONNECTABLE_NONSCANNABLE_DIRECTED",
        "if (!Bluefruit.Advertising.start(0))",
    ):
        assert contract in example
    print("PASS canonical Bluefruit directed-advertising API and scheduling contract")


def validate_controller_pdu_contract() -> None:
    header = source(HAL / "nrf54l15_hal.h")
    setup = source(HAL / "nrf54l15_hal_parts/nrf54l15_hal_ble_core_setup.inc")
    gatt = source(HAL / "nrf54l15_hal_parts/nrf54l15_hal_ble_custom_gatt.inc")

    assert "bool setDirectedAdvertisingTarget(" in header
    assert "void clearDirectedAdvertisingTarget();" in header
    for contract in (
        "bool directedTargetValid_",
        "BleAddressType directedTargetAddressType_",
        "uint8_t directedTargetAddress_[6]",
    ):
        assert contract in header
    for contract in (
        "out->directedTargetValid = directedTargetValid_",
        "out->directedTargetAddressType = directedTargetAddressType_",
        "memcpy(out->directedTargetAddress, directedTargetAddress_",
        "directedTargetValid_ = snapshot.directedTargetValid",
    ):
        assert contract in setup

    builder = function_body(
        gatt,
        "bool BleRadio::buildAdvertisingPacket()",
        "bool BleRadio::setScanResponseData(",
    )
    for contract in (
        "directed && !directedTargetValid_",
        "sizeof(address_) + sizeof(directedTargetAddress_)",
        "header |= (1U << 5U)",
        "header |= (1U << 6U)",
        "header |= (1U << 7U)",
        "memcpy(&txPacket_[2 + sizeof(address_)], directedTargetAddress_",
    ):
        assert contract in builder
    assert "directed ? (sizeof(address_) + sizeof(directedTargetAddress_))" in builder
    print("PASS ADV_DIRECT_IND AdvA/TargetA and header-bit construction contract")


def validate_controller_filter_contract() -> None:
    advertising = source(
        HAL / "nrf54l15_hal_parts/nrf54l15_hal_ble_advertising.inc"
    )
    connections = source(
        HAL / "nrf54l15_hal_parts/nrf54l15_hal_ble_scanning_connections.inc"
    )
    header = source(HAL / "nrf54l15_hal.h")

    interaction = function_body(
        advertising,
        "bool BleRadio::advertiseInteractOncePrepared(",
        "bool BleRadio::advertiseInteractOnce(",
    )
    for contract in (
        "pduType_ != BleAdvPduType::kAdvDirectInd",
        "const bool directedPeerMatch =",
        "directedInitiatorMatchesTarget(scannerOrInitiator, txAddrRandom)",
        "pduType == kBlePduScanReq && !directed",
        "addressMatch && directedPeerMatch",
    ):
        assert contract in interaction

    assert connections.count("const bool directedAtLocalAddress =") == 2
    assert connections.count("directedTargetMatchesLocalAddress(&advPayload[6]") == 2
    assert connections.count("(directed && directedAtLocalAddress)") == 2
    assert "return (!directed && advPayloadLength > kBleLegacyAddressLength)" in header
    assert "directed ? 0U : static_cast<uint8_t>(payloadLength - 6U)" in source(
        BLUEFRUIT / "bluefruit.cpp"
    )
    print("PASS directed InitA/TargetA filtering and non-scannable report contract")


def main() -> int:
    validate_bluefruit_public_contract()
    validate_controller_pdu_contract()
    validate_controller_filter_contract()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
