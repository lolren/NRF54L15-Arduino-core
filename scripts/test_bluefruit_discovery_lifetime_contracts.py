#!/usr/bin/env python3
"""Validate Bluefruit central discovery and client lifetime contracts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BLUEFRUIT = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib"
)
SOURCE = (BLUEFRUIT / "src/bluefruit.cpp").read_text(encoding="utf-8")
HEADER = (BLUEFRUIT / "src/bluefruit.h").read_text(encoding="utf-8")
COMMON = (BLUEFRUIT / "src/bluefruit_common.h").read_text(encoding="utf-8")


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for offset in range(opening, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : offset + 1]
    raise AssertionError(f"unterminated function: {signature}")


def main() -> int:
    assert "struct ble_gattc_handle_range_t" in COMMON
    assert "class BLEDiscovery {};" not in HEADER
    assert "kMaxClientServices = 20U" in SOURCE
    assert "kMaxClientCharacteristics = 40U" in SOURCE
    assert "void setHandleRange(ble_gattc_handle_range_t handle_range);" in HEADER
    assert HEADER.count("uint8_t discoverCharacteristic(uint16_t conn_handle,") == 9
    assert "BLEClientCharacteristic* characteristics[]," in HEADER

    discovery = body(
        SOURCE,
        "uint8_t BLEDiscovery::discoverCharacteristic(\n    uint16_t conn_handle,",
    )
    assert "!clientReady(conn_handle)" in discovery
    assert "characteristics == nullptr" in discovery
    assert "handle_range_.start_handle > handle_range_.end_handle" in discovery
    assert "characteristics[priorIndex] == characteristic" in discovery
    assert "duplicateObject" in discovery
    assert "prior->uuid != characteristic->uuid" in discovery
    assert "prior->end_handle_ + 1U" in discovery
    assert "discoverCharacteristicSync(" in discovery
    assert "manager().connectionGeneration()" in discovery
    assert "characteristic->discovered_ = true;" in discovery
    print("PASS bounded bulk characteristic discovery and duplicate UUID pagination")

    read_by_uuid = body(SOURCE, "uint16_t BLEGatt::readCharByUuid(")
    assert "!clientReady(conn_hdl)" in read_by_uuid
    assert "start_hdl == 0U || start_hdl > end_hdl" in read_by_uuid
    assert "discoverCharacteristicSync(" in read_by_uuid
    assert "readHandleSync(valueHandle" in read_by_uuid
    assert "uint16_t readCharByUuid(" in HEADER
    print("PASS handle-range Read Characteristic by UUID facade")

    unregister_characteristic = body(
        SOURCE, "void unregisterClientCharacteristic("
    )
    unregister_service = body(SOURCE, "void unregisterClientService(")
    characteristic_destructor = body(
        SOURCE, "BLEClientCharacteristic::~BLEClientCharacteristic()"
    )
    service_destructor = body(SOURCE, "BLEClientService::~BLEClientService()")
    uart_destructor = body(SOURCE, "BLEClientUart::~BLEClientUart()")

    assert "clearDeferredClientValueEvents(characteristic);" in unregister_characteristic
    assert "--client_characteristic_count_;" in unregister_characteristic
    assert "client_characteristics_[client_characteristic_count_] = nullptr;" in unregister_characteristic
    assert "characteristic->service_ = nullptr;" in unregister_characteristic
    assert "characteristic->begun_ = false;" in unregister_characteristic

    assert "characteristic->service_ == service" in unregister_service
    assert "unregisterClientCharacteristic(characteristic);" in unregister_service
    assert "--client_service_count_;" in unregister_service
    assert "BLEClientService::lastService == service" in unregister_service
    assert "client_services_[client_service_count_ - 1U]" in unregister_service

    assert "if (begun_)" in characteristic_destructor
    assert "managerIfAlive()" in characteristic_destructor
    assert "activeManager->unregisterClientCharacteristic(this)" in characteristic_destructor
    assert "if (begun_)" in service_destructor
    assert "managerIfAlive()" in service_destructor
    assert "activeManager->unregisterClientService(this)" in service_destructor
    manager_destructor = body(SOURCE, "~BluefruitCompatManager()")
    assert "g_bluefruitCompatManager = nullptr;" in manager_destructor
    assert "--instance_count_;" in uart_destructor
    assert "instances_[instance_count_] = nullptr;" in uart_destructor
    uart_begin = body(SOURCE, "bool BLEClientUart::begin()")
    assert "if (!instance_registered_)" in uart_destructor
    assert "if (!instance_registered_)" in uart_begin
    assert "if (instance_count_ >= kMaxInstances)" in uart_begin
    assert "instances_[instance_count_++] = this;" in uart_begin
    assert "virtual ~BLEClientService();" in HEADER
    assert "BLEClientCharacteristic(const BLEClientCharacteristic&) = delete;" in HEADER
    assert "BLEClientService(const BLEClientService&) = delete;" in HEADER
    assert "BLEClientUart(const BLEClientUart&) = delete;" in HEADER
    callback_dispatch = body(SOURCE, "void invokeBluefruitUserCallback(")
    assert "Args&&... args" in SOURCE
    assert "callback(std::forward<Args>(args)...);" in callback_dispatch
    print("PASS client pointer deregistration, parent detachment, and non-copyable ownership")

    example = (
        BLUEFRUIT
        / "examples/Central/central_gatt_discovery/central_gatt_discovery.ino"
    ).read_text(encoding="utf-8")
    assert "Bluefruit.Discovery.setHandleRange(genericAccess.getHandleRange())" in example
    assert "Bluefruit.Discovery.discoverCharacteristic(" in example
    assert "Bluefruit.Gatt.readCharByUuid(" in example
    assert "deviceName.begin(&genericAccess);" in example
    print("PASS central discovery example uses explicit persistent parent ownership")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
