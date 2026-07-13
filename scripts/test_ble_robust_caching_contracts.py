#!/usr/bin/env python3
"""Contracts for GATT Robust Caching server behavior."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
PARTS = ROOT / "hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts"
HAL = ROOT / "hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h"
BLUEFRUIT_H = ROOT / "hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/bluefruit.h"
BLUEFRUIT_CPP = ROOT / "hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/bluefruit.cpp"


def require(text: str, needles: tuple[str, ...], label: str) -> None:
    missing = [needle for needle in needles if needle not in text]
    assert not missing, f"{label}: missing {missing}"


def validate_database_hash_vector() -> None:
    # Bluetooth Core, Vol 3, Part G, Appendix B, blocks M0 through M6.
    message = bytes.fromhex(
        "010000280018020003280A0300002A04"
        "000328020500012A0600002801180700"
        "0328200800052A090002290A0003280A"
        "0B00292B0C000328020D002A2B0E0000"
        "2808180F000228140016000F18100003"
        "28A21100182A12000229130000290000"
        "140001280F1815000328021600192A"
    )
    result = subprocess.run(
        ["openssl", "mac", "-macopt", "cipher:AES-128-CBC", "-macopt",
         "hexkey:" + "00" * 16, "CMAC"],
        input=message, capture_output=True, check=True,
    )
    assert result.stdout.strip() == b"F1CA2D48ECF58BAC8A8830BBB9FBA990"
    print("PASS Bluetooth Appendix B Database Hash vector")


def validate_fixed_database_and_cmac_source() -> None:
    bond = (PARTS / "nrf54l15_hal_internal_gatt_bond.inc").read_text()
    crypto = (PARTS / "nrf54l15_hal_internal_crypto_service.inc").read_text()
    att = (PARTS / "nrf54l15_hal_ble_att_l2cap.inc").read_text()
    require(
        bond,
        (
            "kUuidClientSupportedFeatures = 0x2B29U",
            "kUuidDatabaseHash = 0x2B2AU",
            "kHandleGattClientFeaturesDecl = 0x000CU",
            "kHandleGattClientFeaturesValue = 0x000DU",
            "kHandleGattDatabaseHashDecl = 0x000EU",
            "kHandleGattDatabaseHashValue = 0x000FU",
            "kAttErrDatabaseOutOfSync = 0x12U",
            "kAttErrValueNotAllowed = 0x13U",
        ),
        "fixed GATT robust-caching attributes",
    )
    require(
        crypto,
        ("class AesCmacStream", "generateAesCmacSubkeys", "processBlock", "finish"),
        "incremental AES-CMAC",
    )
    require(
        att,
        (
            "bool BleRadio::rebuildGattDatabaseHash()",
            "kUuidPrimaryService",
            "kUuidCharacteristic",
            "kUuidClientCharacteristicConfig",
            "kUuidCharacteristicUserDescription",
            "kUuidCharacteristicPresentationFormat",
            "gattDatabaseHashValid_ = cmac.finish(gattDatabaseHash_)",
        ),
        "database hash construction",
    )
    print("PASS fixed GATT attributes and incremental Database Hash construction")


def validate_persistence_and_state_machine() -> None:
    bond = (PARTS / "nrf54l15_hal_internal_gatt_bond.inc").read_text()
    security = (PARTS / "nrf54l15_hal_ble_ll_security.inc").read_text()
    att = (PARTS / "nrf54l15_hal_ble_att_l2cap.inc").read_text()
    tail = (PARTS / "nrf54l15_hal_ble_peripheral_event_tail.inc").read_text()
    require(
        bond,
        (
            "kBleCccdFixedClientFeaturesMask",
            "(connectionGattClientSupportedFeatures_ & 0x07U) << 2U",
            "(record.fixedCccdFlags & kBleCccdFixedClientFeaturesMask) >> 2U",
            "(connectionGattClientSupportedFeatures_ & ~value[0]) != 0U",
            "setGattClientChangeAware(bool aware, bool clearPendingChange)",
        ),
        "per-bond CSF persistence",
    )
    assert "(connectionGattClientSupportedFeatures_ & 0x07U) << 2U" in security
    require(
        att,
        (
            "connectionGattOutOfSyncSent_ || connectionGattDatabaseHashRead_",
            "GATT_DATABASE_OUT_OF_SYNC",
            "kAttErrDatabaseOutOfSync",
            "GATT_ROBUST_CACHE_COMMAND_IGNORED",
            "type16 == kUuidDatabaseHash",
            "connectionGattDatabaseHashRead_ = true",
        ),
        "change-unaware ATT enforcement",
    )
    assert tail.count("connectionGattClientChangeAware_") >= 2
    print("PASS persistent CSF and change-aware ATT/HVX state machine")


def validate_public_contracts() -> None:
    hal = HAL.read_text()
    bluefruit_h = BLUEFRUIT_H.read_text()
    bluefruit_cpp = BLUEFRUIT_CPP.read_text()
    require(
        hal,
        (
            "bool getGattDatabaseHash(uint8_t outHash[16]) const",
            "uint8_t gattClientSupportedFeatures() const",
            "bool gattClientChangeAware() const",
        ),
        "HAL diagnostics",
    )
    require(
        bluefruit_h,
        (
            "bool databaseHash(uint8_t hash[16]) const",
            "uint8_t clientSupportedFeatures() const",
            "bool clientChangeAware() const",
        ),
        "Bluefruit diagnostics",
    )
    require(
        bluefruit_cpp,
        ("'G', 'A', 'T', 'T', '-', 'R', 'C', '-', '2'",
         "service_changed_enabled_(true)"),
        "schema epoch and default",
    )
    print("PASS HAL and Bluefruit Robust Caching diagnostics")


def validate_model() -> None:
    features = 0
    aware = True
    pending_change = False
    out_of_sync_sent = False
    hash_read = False

    def write_features(value: int) -> bool:
        nonlocal features, aware
        if value & ~0x07 or features & ~value:
            return False
        features = value
        if value & 1 and pending_change:
            aware = False
        return True

    assert write_features(0x01)
    assert not write_features(0x00)
    pending_change = True
    aware = False
    assert not aware
    out_of_sync_sent = True
    if out_of_sync_sent or hash_read:
        aware = True
        pending_change = False
    assert aware and not pending_change
    print("PASS monotonic features and single-bearer awareness model")


def main() -> int:
    validate_database_hash_vector()
    validate_fixed_database_and_cmac_source()
    validate_persistence_and_state_machine()
    validate_public_contracts()
    validate_model()
    print("PASS all GATT Robust Caching contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
