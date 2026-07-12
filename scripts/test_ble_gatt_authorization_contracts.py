#!/usr/bin/env python3
"""Static contracts for ISR-safe Bluefruit GATT authorization support."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
BLUEFRUIT = PLATFORM / "libraries/Bluefruit52Lib"
HAL = PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"


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


def case_block(source: str, label: str, next_label: str) -> str:
    start = source.index(label)
    end = source.index(next_label, start)
    return source[start:end]


def main() -> None:
    common = (BLUEFRUIT / "src/bluefruit_common.h").read_text(encoding="utf-8")
    for field in (
        "uint16_t handle;",
        "ble_uuid_t uuid;",
        "uint8_t op;",
        "uint8_t auth_required;",
        "uint16_t offset;",
        "uint16_t len;",
        "uint8_t data[1];",
    ):
        assert field in common
    for symbol in (
        "BLE_GATTS_OP_WRITE_REQ",
        "BLE_GATTS_OP_PREP_WRITE_REQ",
        "BLE_GATTS_OP_EXEC_WRITE_REQ_NOW",
        "BLE_GATTS_AUTHORIZE_TYPE_READ",
        "BLE_GATTS_AUTHORIZE_TYPE_WRITE",
        "BLE_GATT_STATUS_ATTERR_INSUF_AUTHORIZATION",
        "BLE_GATT_STATUS_ATTERR_APP_BEGIN",
        "sd_ble_gatts_rw_authorize_reply(",
    ):
        assert symbol in common

    header = (HAL / "nrf54l15_hal.h").read_text(encoding="utf-8")
    for contract in (
        "enum class BleGattAuthorizeType",
        "enum class BleGattAuthorizeReplyResult",
        "struct BleGattAuthorizeRequest",
        "bool readAuthorize;",
        "bool writeAuthorize;",
        "struct BleGattAuthorizationState",
        "bool pending;",
        "bool callbackPending;",
        "bool responseReady;",
        "uint32_t generation;",
        "uint32_t deadlineMs;",
        "setCustomGattAuthorization(",
        "consumeCustomGattAuthorizeRequest(",
        "replyCustomGattAuthorization(",
    ):
        assert contract in header

    bluefruit = (BLUEFRUIT / "src/bluefruit.cpp").read_text(encoding="utf-8")
    begin = body(bluefruit, "err_t BLECharacteristic::begin()")
    assert "setCustomGattAuthorization(" in begin
    assert "_rd_authorize_cb != nullptr" in begin
    assert "_wr_authorize_cb != nullptr" in begin
    assert "return ERROR_NOT_SUPPORTED" not in begin

    dispatch = body(bluefruit, "void dispatchGattAuthorizationRequest()")
    assert "consumeCustomGattAuthorizeRequest" in dispatch
    assert "alignas(ble_gatts_evt_write_t)" in dispatch
    assert "event->auth_required = 1U" in dispatch
    assert "_rd_authorize_cb" in dispatch
    assert "_wr_authorize_cb" in dispatch
    assert "BLE_GATT_STATUS_ATTERR_INSUF_AUTHORIZATION" in dispatch

    shim = body(bluefruit, 'extern "C" uint32_t sd_ble_gatts_rw_authorize_reply(')
    assert "BLE_ERROR_INVALID_CONN_HANDLE" in shim
    assert "NRF_ERROR_INVALID_ADDR" in shim
    assert "replyCustomGattAuthorization(" in shim
    assert "NRF_ERROR_TIMEOUT" in shim

    att = (HAL / "nrf54l15_hal_parts/nrf54l15_hal_ble_att_l2cap.inc").read_text(
        encoding="utf-8"
    )
    read_case = case_block(att, "case kAttOpReadReq:", "case kAttOpReadBlobReq:")
    blob_case = case_block(
        att, "case kAttOpReadBlobReq:", "case kAttOpReadMultipleReq:"
    )
    write_case = case_block(att, "case kAttOpWriteReq:", "case kAttOpWriteCmd:")
    prepare_case = case_block(
        att, "case kAttOpPrepareWriteReq:", "case kAttOpExecuteWriteReq:"
    )
    execute_case = case_block(
        att, "case kAttOpExecuteWriteReq:", "case kAttOpHandleValueCfm:"
    )
    for read_path in (read_case, blob_case):
        assert "readAttributeValue(" in read_path
        assert "beginCustomGattAuthorization(" in read_path
        assert read_path.index("readAttributeValue(") < read_path.index(
            "beginCustomGattAuthorization("
        )
        assert "*outAttResponseLength = 0U" in read_path
    assert "customGattWritePermissionSatisfied" in write_case
    assert "beginCustomGattAuthorization(" in write_case
    assert "writeCustomGattCharacteristic(" in write_case
    assert write_case.index("beginCustomGattAuthorization(") < write_case.index(
        "writeCustomGattCharacteristic("
    )
    assert "beginCustomGattAuthorization(" not in prepare_case
    assert "writeCustomGattCharacteristic(" in execute_case

    reply = body(att, "BleGattAuthorizeReplyResult BleRadio::replyCustomGattAuthorization(")
    generation_check = "connectionGattAuthorization_.generation != requestGeneration"
    commit = "applyCustomGattCharacteristicValueWrite("
    publish = "connectionGattAuthorization_.responseReady = true;"
    assert generation_check in reply
    assert commit in reply
    assert publish in reply
    assert reply.index(generation_check) < reply.index(commit) < reply.index(publish)
    assert reply.index(generation_check) < reply.index(
        "commitCharacteristic->valueLength = readCandidateLength;"
    ) < reply.index(publish)
    assert "deferredGattWriteCount_ >= deferredDepth" in reply
    assert "!update || offset != 0U" in reply

    timeout = body(att, "void BleRadio::serviceCustomGattAuthorizationTimeout()")
    assert "connectionGattAuthorization_.pending ||" in timeout
    assert "connectionGattAuthorization_.responseReady" in timeout
    assert "disconnectWithReason" in timeout

    custom = (
        HAL / "nrf54l15_hal_parts/nrf54l15_hal_ble_custom_gatt.inc"
    ).read_text(encoding="utf-8")
    clear = body(custom, "void BleRadio::clearCustomGattConnectionState()")
    assert "clearCustomGattAuthorizationState();" in clear

    tail = (
        HAL / "nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tail.inc"
    ).read_text(encoding="utf-8")
    auth_response = "takeCustomGattAuthorizationResponse("
    service_changed = "connectionServiceChangedIndicationsEnabled_ &&"
    assert auth_response in tail
    assert tail.index(auth_response) < tail.index(service_changed)
    service_gate = tail[tail.index(service_changed) : tail.index(service_changed) + 700]
    assert "connectionCustomIndicationAwaitingHandle_ == 0U" in service_gate

    example = (
        BLUEFRUIT
        / "examples/Diagnostics/gatt_authorization/gatt_authorization.ino"
    ).read_text(encoding="utf-8")
    assert "setReadAuthorizeCallback" in example
    assert "setWriteAuthorizeCallback" in example
    assert "BLE_GATT_STATUS_ATTERR_INSUF_AUTHORIZATION" in example
    assert "sd_ble_gatts_rw_authorize_reply" in example

    print(
        "PASS ISR-safe GATT authorization ABI, deferral, atomic reply, timeout, "
        "long-write, and indication-serialization contracts"
    )


if __name__ == "__main__":
    main()
