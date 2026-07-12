#!/usr/bin/env python3
"""Static contracts for Bluefruit central indication and typed client APIs."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
BLUEFRUIT = PLATFORM / "libraries/Bluefruit52Lib"
SOURCE_PATH = BLUEFRUIT / "src/bluefruit.cpp"
HEADER_PATH = BLUEFRUIT / "src/bluefruit.h"
HAL_PARTS = (
    PLATFORM
    / "libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts"
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


def main() -> None:
    header = HEADER_PATH.read_text(encoding="utf-8")
    source = SOURCE_PATH.read_text(encoding="utf-8")

    for api in (
        "typedef void (*indicate_cb_t)",
        "void setIndicateCallback(indicate_cb_t fp, bool deferred = true)",
        "uint16_t connHandle() const",
        "uint16_t valueHandle() const",
        "uint8_t properties() const",
        "uint16_t read16();",
        "uint32_t read32();",
        "uint16_t write16(uint16_t value);",
        "uint16_t write32(uint32_t value);",
        "uint16_t write32(int value);",
        "uint16_t write_resp(const void* buffer, uint16_t len);",
        "uint16_t write16_resp(uint16_t value);",
        "uint16_t write32_resp(uint32_t value);",
        "bool writeCCCD(uint16_t value);",
        "bool enableIndicate();",
        "bool disableIndicate();",
        "uint32_t discovery_generation_;",
    ):
        assert api in header, api

    constructor_start = source.index(
        "BLEClientCharacteristic::BLEClientCharacteristic()"
    )
    constructor_end = source.index(
        "BLEClientCharacteristic::BLEClientCharacteristic(BLEUuid", constructor_start
    )
    constructor = source[constructor_start:constructor_end]
    assert "indicate_callback_(nullptr)" in constructor
    assert "indicate_deferred_(true)" in constructor
    assert "cccd_value_(0U)" in constructor
    assert "discovery_generation_(0U)" in constructor

    typed_read16 = body(source, "uint16_t BLEClientCharacteristic::read16()")
    typed_read32 = body(source, "uint32_t BLEClientCharacteristic::read32()")
    assert "read(&value, sizeof(value)) == sizeof(value)" in typed_read16
    assert "read(&value, sizeof(value)) == sizeof(value)" in typed_read32
    for signature in (
        "uint16_t BLEClientCharacteristic::write16(uint16_t value)",
        "uint16_t BLEClientCharacteristic::write32(uint32_t value)",
        "uint16_t BLEClientCharacteristic::write32(int value)",
        "uint16_t BLEClientCharacteristic::write16_resp(",
        "uint16_t BLEClientCharacteristic::write32_resp(uint32_t value)",
    ):
        assert "sizeof(" in body(source, signature)
    assert "write(buffer, len, true)" in body(
        source, "uint16_t BLEClientCharacteristic::write_resp("
    )

    write_cccd = body(source, "bool BLEClientCharacteristic::writeCCCD(")
    assert "(value & ~0x0003U) != 0U" in write_cccd
    assert "kBleGattPropNotify" in write_cccd
    assert "kBleGattPropIndicate" in write_cccd
    assert "discoverCccdHandleSync(" in write_cccd
    assert "writeHandleSync(cccd_handle_" in write_cccd
    assert "cccd_value_ = value;" in write_cccd
    assert "cccd_value_ | 0x0001U" in body(
        source, "bool BLEClientCharacteristic::enableNotify()"
    )
    assert "cccd_value_ | 0x0002U" in body(
        source, "bool BLEClientCharacteristic::enableIndicate()"
    )
    assert "cccd_value_ & ~0x0002U" in body(
        source, "bool BLEClientCharacteristic::disableIndicate()"
    )

    callback_select = body(
        source, "BLEClientCharacteristic::callbackForValueEvent("
    )
    assert "att_opcode == kAttOpHandleValueInd" in callback_select
    assert "indicate_callback_ != nullptr" in callback_select
    assert "return indicate_callback_;" in callback_select
    assert "return notify_callback_;" in callback_select
    assert "historically received indications" in callback_select

    queue = body(source, "bool enqueueDeferredClientValueEvent(")
    assert "event.att_opcode = attOpcode;" in queue
    assert "event.connection_generation = connectionGeneration;" in queue
    assert "connectionGeneration != connection_generation_" in queue
    dispatch = body(source, "void dispatchDeferredUserCallbacks()")
    assert "eventGeneration == connection_generation_" in dispatch
    assert "BleConnectionRole::kCentral" in dispatch
    assert "characteristic->discovery_generation_ == eventGeneration" in dispatch
    assert dispatch.index("--deferred_client_value_event_count_;") < dispatch.index(
        "dispatchValueEventCallback(attOpcode);"
    )

    connection_edge = body(source, "void handleConnectionEdge(bool connected)")
    assert connection_edge.index("advanceConnectionGeneration();") < connection_edge.index(
        "if (connected)"
    )
    discover = body(source, "bool BLEClientCharacteristic::discover()")
    assert "discovery_generation_ = manager().connectionGeneration();" in discover
    assert discover.index("discovery_generation_ =") < discover.index(
        "discovered_ = true;"
    )
    reset = body(source, "void BLEClientCharacteristic::resetDiscovery()")
    assert "clearDeferredClientValueEvents(this)" in reset
    assert "discovery_generation_ = 0U;" in reset

    client_event = body(
        source, "void handleClientConnectionEvent(const BleConnectionEvent& event)"
    )
    assert "characteristic->handleValueEvent(value, valueLength, attOpcode" in client_event
    assert "kAttOpHandleValueCfm" not in client_event
    assert "queueAttRequest" not in client_event
    central_event = (
        HAL_PARTS / "nrf54l15_hal_ble_central_event.inc"
    ).read_text(encoding="utf-8")
    assert "rxAttOpcode == kAttOpHandleValueInd" in central_event
    assert "connectionPendingTxPayload_[4] = kAttOpHandleValueCfm;" in central_event

    peripheral = (
        BLUEFRUIT
        / "examples/Peripheral/indication_peripheral/indication_peripheral.ino"
    ).read_text(encoding="utf-8")
    central = (
        BLUEFRUIT / "examples/Central/central_indication/central_indication.ino"
    ).read_text(encoding="utf-8")
    assert "CHR_PROPS_READ | CHR_PROPS_INDICATE" in peripheral
    assert "indicationCounter.indicate32(next)" in peripheral
    assert "setIndicateCallback(indicationCallback)" in central
    assert "setNotifyCallback(unexpectedNotificationCallback)" in central
    assert "indicationCounter.enableIndicate()" in central
    assert "indicationCounter.read32()" in central
    assert "indicationCounter.valueHandle()" in central
    assert "indicationCounter.properties()" in central

    print(
        "PASS central indication callbacks, CCCD control, typed APIs, "
        "generation isolation, and paired examples"
    )


if __name__ == "__main__":
    main()
