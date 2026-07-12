#!/usr/bin/env python3
"""Host-side contracts for bonded GATT Service Changed state."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
BLUEFRUIT = PLATFORM / "libraries/Bluefruit52Lib/src"
HAL = PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"
PARTS = HAL / "nrf54l15_hal_parts"


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


@dataclass
class PendingModel:
    start: int = 0
    end: int = 0
    generation: int = 0
    sent_generation: int = 0

    def mark(self, start: int, end: int) -> None:
        assert 0 < start <= end <= 0xFFFF
        if self.start == 0:
            self.start, self.end = start, end
        else:
            self.start = min(self.start, start)
            self.end = max(self.end, end)
        self.generation += 1

    def send(self) -> None:
        assert self.start != 0
        self.sent_generation = self.generation

    def confirm(self) -> bool:
        if self.sent_generation != self.generation:
            return False
        self.start = 0
        self.end = 0
        self.generation += 1
        return True


def validate_public_and_hash_contracts() -> None:
    header = (BLUEFRUIT / "bluefruit.h").read_text(encoding="utf-8")
    source = (BLUEFRUIT / "bluefruit.cpp").read_text(encoding="utf-8")
    for contract in (
        "bool serviceChanged(uint16_t start_handle",
        "bool serviceChangedPending(uint16_t* start_handle",
        "uint32_t serviceChangedFingerprint() const",
        "bool service_changed_enabled_;",
    ):
        assert contract in header

    config = body(source, "void AdafruitBluefruit::configServiceChanged(")
    assert "service_changed_enabled_ = changed;" in config
    assert "configureServiceChanged(changed)" in config
    assert "markGattServiceChanged" not in config

    advertising_start = body(source, "bool BLEAdvertising::start(")
    assert advertising_start.index("finalizeGattSchema()") < advertising_start.index(
        "running_ = true;"
    )
    service_begin = body(source, "err_t BLEService::begin()")
    characteristic_begin = body(source, "err_t BLECharacteristic::begin()")
    assert "noteGattService(this)" in service_begin
    assert "noteGattCharacteristic(" in characteristic_begin
    hash_characteristic = body(source, "void noteGattCharacteristic(")
    for structural_field in (
        "_properties",
        "readPermission",
        "writePermission",
        "_max_len",
        "_fixed_len",
        "cccd_handle",
        "presentationFormatHandle",
        "reportReferenceHandle",
    ):
        assert structural_field in hash_characteristic
    for mutable_value in ("_value_len", "->_value", "initialValue"):
        assert mutable_value not in hash_characteristic


def validate_persistence_contracts() -> None:
    store = (PARTS / "nrf54l15_hal_internal_gatt_bond.inc").read_text(
        encoding="utf-8"
    )
    timing = (PARTS / "nrf54l15_hal_internal_ble_timing.inc").read_text(
        encoding="utf-8"
    )
    assert "kBleCccdRetentionVersion = 2U" in timing
    for contract in (
        "struct BleCccdBondRecordV1",
        "sizeof(BleCccdBondRecordV1) == 128U",
        "sizeof(BleCccdBondRecord) == 128U",
        "sizeof(BleCccdRetentionBlobV1) == 140U",
        "sizeof(BleCccdRetentionBlob) == 140U",
        "uint32_t schemaFingerprint;",
        "uint16_t serviceChangedStartHandle;",
        "uint16_t serviceChangedEndHandle;",
    ):
        assert contract in store

    migration = body(store, "bool migrateCccdBondRecordV1(")
    assert "for (uint8_t i = 0U; i < legacy.entryCount; ++i)" in migration
    assert "kBleCccdFixedServiceChanged" in migration
    assert "kBleCccdFixedBattery" in migration
    assert "appendCccdBondEntry(outRecord, entry.handle, entry.value)" in migration

    persist = body(store, "bool BleRadio::persistBondedCccdState(")
    assert "record.fixedCccdFlags |= kBleCccdFixedServiceChanged" in persist
    assert "record.fixedCccdFlags |= kBleCccdFixedBattery" in persist
    assert "record.schemaFingerprint = bondedGattSchemaFingerprint_" in persist
    assert "record.serviceChangedStartHandle = bondedServiceChangedStartHandle_" in persist
    assert "record.serviceChangedEndHandle = bondedServiceChangedEndHandle_" in persist
    assert "appendCccdBondEntry(&record, kHandleGattServiceChangedCccd" not in persist

    restore = body(store, "bool BleRadio::restoreBondedCccdState()")
    assert "readFlashCccdBondRecord(&record, &migrated)" in restore
    assert "connectionServiceChangedCccdRestored_ = true" in restore
    assert "(void)persistBondedCccdState();" in restore

    commit = body(store, "bool BleRadio::commitGattSchemaFingerprint(")
    baseline = commit.index("bondedGattSchemaFingerprint_ == 0U")
    mismatch = commit.index("bondedGattSchemaFingerprint_ != fingerprint")
    assert baseline < mismatch
    assert "Establish a baseline once" in commit
    assert "markGattServiceChanged(0x0001U, 0xFFFFU)" in commit
    assert "Before a peer is identified" in commit

    mark = body(store, "bool BleRadio::markGattServiceChanged(")
    assert "startHandle < bondedServiceChangedStartHandle_" in mark
    assert "endHandle > bondedServiceChangedEndHandle_" in mark
    assert "persistBondedCccdState()" in mark

    clear = body(store, "void BleRadio::resetBondedServiceChangedState(")
    assert "bondedServiceChangedStartHandle_ = 0U" in clear
    bond_persist = body(
        (PARTS / "nrf54l15_hal_ble_ll_security.inc").read_text(encoding="utf-8"),
        "bool BleRadio::persistBondRecord(",
    )
    assert "if (!sameBond)" in bond_persist
    assert "resetBondedServiceChangedState(gattSchemaFingerprint_)" in bond_persist


def validate_delivery_contracts() -> None:
    att = (PARTS / "nrf54l15_hal_ble_att_l2cap.inc").read_text(encoding="utf-8")
    tail = (PARTS / "nrf54l15_hal_ble_peripheral_event_tail.inc").read_text(
        encoding="utf-8"
    )
    connection = (PARTS / "nrf54l15_hal_ble_connection_api.inc").read_text(
        encoding="utf-8"
    )
    tx_window = tail[tail.index("connectionServiceChangedCccdRestored_") :]
    tx_window = tx_window[: tx_window.index("if ((deferredLength == 0U)", 1)]
    for contract in (
        "connectionEncRxEnabled_ && connectionEncTxEnabled_",
        "bondedServiceChangedStartHandle_",
        "bondedServiceChangedEndHandle_",
        "connectionServiceChangedIndicationGeneration_ =",
        "bondedServiceChangedGeneration_",
    ):
        assert contract in tx_window

    confirmation = att[att.index("case kAttOpHandleValueCfm:") :]
    confirmation = confirmation[: confirmation.index("case kAttOpWriteReq:")]
    assert "confirmsServiceChanged" in confirmation
    assert "confirmBondedServiceChanged(confirmedGeneration)" in confirmation
    assert "bondedServiceChangedStartHandle_ = 0U" not in confirmation

    apply_cccd = body(att, "bool BleRadio::applyCccdState(")
    disabled = apply_cccd[apply_cccd.index("if (!enableIndication)") :]
    disabled = disabled[: disabled.index("connectionServiceChangedIndicationsEnabled_")]
    assert "bondedServiceChangedStartHandle_" not in disabled
    assert "bondedServiceChangedEndHandle_" not in disabled

    disconnect = body(connection, "bool BleRadio::disconnectWithReason(")
    assert "connectionServiceChangedIndicationPending_ = false" in disconnect
    assert "connectionServiceChangedIndicationAwaitingConfirm_ = false" in disconnect
    assert "bondedServiceChangedStartHandle_ = 0U" not in disconnect
    assert "bondedServiceChangedEndHandle_ = 0U" not in disconnect


def validate_state_model() -> None:
    state = PendingModel()
    state.mark(0x20, 0x30)
    state.mark(0x10, 0x25)
    assert (state.start, state.end) == (0x10, 0x30)
    state.send()
    state.mark(0x40, 0x50)
    assert not state.confirm()
    assert (state.start, state.end) == (0x10, 0x50)
    state.send()
    assert state.confirm()
    assert (state.start, state.end) == (0, 0)


def main() -> int:
    validate_public_and_hash_contracts()
    validate_persistence_contracts()
    validate_delivery_contracts()
    validate_state_model()
    print("PASS bonded Service Changed persistence, hashing, and confirmation contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
