#!/usr/bin/env python3
"""Validate release-critical Bluefruit client and HID contracts."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BLUEFRUIT = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib"
)
SOURCE = BLUEFRUIT / "src/bluefruit.cpp"
HEADER = BLUEFRUIT / "src/bluefruit.h"
COMMON = BLUEFRUIT / "src/bluefruit_common.h"


def function_body(source: str, signature: str) -> str:
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
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    common = COMMON.read_text(encoding="utf-8")

    zephyr_hid_info_start = source.index(
        "const uint8_t kHidZephyrInfoValue[] = {"
    )
    zephyr_hid_info_end = source.index("};", zephyr_hid_info_start)
    zephyr_hid_info = source[zephyr_hid_info_start:zephyr_hid_info_end]
    assert "0x11U, 0x01U" in zephyr_hid_info
    assert "0x00U, 0x00U" not in zephyr_hid_info
    print("PASS compact HID mouse advertises conformant HID version 1.11")

    hid_begin = function_body(source, "err_t BLEHidAdafruit::begin()")
    compact_branch = hid_begin[
        hid_begin.index("if (zephyr_compatible_mouse_)") :
        hid_begin.index("const uint8_t protocol =", hid_begin.index("if (zephyr_compatible_mouse_)"))
    ]
    assert compact_branch.count("SECMODE_ENC_NO_MITM") >= 5
    assert "setReportRefDescriptorPermission(SECMODE_ENC_NO_MITM)" in compact_branch
    hid_mouse_report = function_body(
        source,
        "bool BLEHidAdafruit::mouseReport(uint16_t conn_hdl, hid_mouse_report_t* report)",
    )
    assert "const uint8_t emptyBootMouse[3]" in hid_begin
    assert "emptyBootMouse, sizeof(emptyBootMouse)" in hid_begin
    assert "const uint8_t bootReport[3]" in hid_mouse_report
    assert "boot_mouse_input_.notify(conn_hdl, bootReport, sizeof(bootReport))" in hid_mouse_report
    print("PASS HOGP Boot Mouse characteristics and notifications use three bytes")

    mouse_example = (
        BLUEFRUIT / "examples/HID/blehid_mouse/blehid_mouse.ino"
    ).read_text(encoding="utf-8")
    for token in (
        "Bluefruit.Security.setIOCaps(false, false, false)",
        "blehid.setZephyrCompatibleMouse(true)",
    ):
        assert token in mouse_example, f"portable HID mouse example missing: {token}"
    print("PASS HID mouse example uses broad-host compact-profile interoperability mode")

    client_ready = function_body(source, "bool clientReady(uint16_t connHandle)")
    retry_client = function_body(source, "bool retryClientProcedure(")
    assert "connHandle == 0U" in client_ready
    assert "isConnected()" in client_ready
    assert "BleConnectionRole::kCentral" not in client_ready
    assert "BleConnectionRole::kCentral" not in retry_client
    client_dispatch = function_body(
        source, "void handleClientConnectionEvent(const BleConnectionEvent& event)"
    )
    assert "kAttOpHandleValueNtf" in client_dispatch
    assert "kAttOpHandleValueInd" in client_dispatch
    assert "kAttOpHandleValueCfm" not in client_dispatch
    assert "queueAttRequest" not in client_dispatch
    assert "event.packetIsNew" in client_dispatch
    assert client_dispatch.index("event.packetIsNew") < client_dispatch.index(
        "characteristic->handleValueEvent(value, valueLength, attOpcode"
    )
    att_wait = function_body(source, "AttWaitResult waitForAttOpcode(")
    assert "if (!event.packetIsNew)" in att_wait
    assert att_wait.index("if (event.terminateInd)") < att_wait.index(
        "if (!event.packetIsNew)"
    )
    assert att_wait.index("if (!event.packetIsNew)") < att_wait.index(
        "if (attOpcode == kAttOpErrorRsp"
    )
    assert att_wait.index("if (!event.packetIsNew)") < att_wait.index(
        "if (attOpcode == responseOpcode)"
    )
    idle_service = function_body(source, "void idleService()")
    peripheral_start = idle_service.index(
        "if (radio_.connectionRole() == BleConnectionRole::kPeripheral)"
    )
    central_start = idle_service.index(
        "} else if (radio_.connectionRole() == BleConnectionRole::kCentral)",
        peripheral_start,
    )
    peripheral_branch = idle_service[peripheral_start:central_start]
    assert "processClientBackgroundEvents(4U);" in peripheral_branch
    assert "handleClientConnectionEvent(event);" in peripheral_branch
    assert "dispatchDeferredUserCallbacks();" in peripheral_branch
    capacity_wait = function_body(source, "bool waitForLinkValueCapacity(")
    assert "clientAutomaticMtuCeiling()" in capacity_wait
    assert "clientAutomaticDataLengthCeiling()" in capacity_wait
    print(
        "PASS GATT client procedures, fresh-only ATT dispatch, and disconnect "
        "handling on either GAP role"
    )

    client_solicitation = function_body(
        source,
        "bool BLEAdvertisingData::addService(const BLEClientService& service)",
    )
    assert "kAdTypeSolicited16" in client_solicitation
    assert "kAdTypeSolicited128" in client_solicitation
    assert "value & 0xFFU" in client_solicitation
    assert "service.uuid.uuid128()" in client_solicitation
    assert "return addService(service.uuid);" not in client_solicitation
    assert "BLE_GAP_AD_TYPE_SOLICITED_SERVICE_UUIDS_16BIT 0x14U" in common
    assert "BLE_GAP_AD_TYPE_SOLICITED_SERVICE_UUIDS_128BIT 0x15U" in common
    print("PASS client-service 16/128-bit Service Solicitation AD encoding")

    uuid_canonical = function_body(source, "void bleUuid128ToCanonical(")
    service_begin = function_body(source, "err_t BLEService::begin()")
    characteristic_begin = function_body(source, "err_t BLECharacteristic::begin()")
    assert "canonical[i] = littleEndian[15U - i]" in uuid_canonical
    assert "bleUuid128ToCanonical(uuid.uuid128(), canonicalUuid)" in service_begin
    assert "addCustomGattService128(canonicalUuid" in service_begin
    assert "bleUuid128ToCanonical(uuid.uuid128(), canonicalUuid)" in characteristic_begin
    assert "addCustomGattCharacteristic128WithDescriptors(\n        _service->_handle, canonicalUuid" in characteristic_begin
    print("PASS Bluefruit 128-bit server UUID canonical/wire byte order")

    ancs_get = function_body(
        source,
        "uint16_t BLEAncs::getAttribute(uint32_t uid, uint8_t attr, void* buffer, uint16_t bufsize)",
    )
    ancs_get_app = function_body(
        source,
        "uint16_t BLEAncs::getAppAttribute(const char* appid, uint8_t attr, void* buffer,",
    )
    for getter in (ancs_get, ancs_get_app):
        assert "static_cast<uint8_t*>(buffer)[0] = 0U;" in getter
        assert "bufsize - 1U" in getter
        assert "static_cast<uint8_t*>(buffer)[response_.valueCopied] = 0U;" in getter
        assert "static_cast<uint8_t*>(buffer)[copied] = 0U;" in getter
    ancs_handle = function_body(
        source, "void BLEAncs::handleNotification(uint8_t* data, uint16_t len)"
    )
    assert "eventID > ANCS_EVT_NOTIFICATION_REMOVED" in ancs_handle
    assert "categoryID > ANCS_CAT_ENTERTAINMENT" in ancs_handle
    assert "memcpy(&notification, data, sizeof(notification));" in ancs_handle
    assert "invokeBluefruitUserCallback(notification_callback_, &notification)" in ancs_handle

    ancs_example = (
        BLUEFRUIT / "examples/Services/ancs/ancs.ino"
    ).read_text(encoding="utf-8")
    ancs_oled_example = (
        BLUEFRUIT / "examples/Services/ancs_oled/ancs_oled.ino"
    ).read_text(encoding="utf-8")
    assert "len >= 3 && 0 == memcmp(&buffer[len-3]" in ancs_example
    assert "if ( notifCount >= MAX_COUNT ) return;" in ancs_oled_example
    assert "len >= 3 && 0 == memcmp(&myNtf->title[len-3]" in ancs_oled_example
    assert "snprintf(tempbuf, sizeof(tempbuf)" in ancs_oled_example
    assert "static_cast<uint32_t>(digitalRead(BUTTON_A))" in ancs_oled_example
    print("PASS ANCS terminated strings and example bounds contracts")

    long_read = function_body(source, "uint16_t readHandleSync(")
    assert "while (copied < bufferLen)" in long_read
    assert "kAttOpReadBlobReq" in long_read
    assert "kAttOpReadBlobRsp" in long_read
    assert "writeLe16(&request[3], copied);" in long_read
    assert "fragmentLen < responseCapacity" in long_read
    assert "fragmentLen == 0U" in long_read
    assert "kAttErrInvalidOffset" in long_read
    assert "kAttErrAttributeNotLong" in long_read
    client_read = function_body(
        source, "uint16_t BLEClientCharacteristic::read(void* buffer, uint16_t len)"
    )
    assert "readHandleSync(value_handle_, output, len)" in client_read
    assert "uint8_t scratch" not in client_read
    print("PASS Bluefruit client Read Blob continuation and terminal-error contract")

    deferred_enqueue = function_body(source, "bool enqueueDeferredClientValueEvent(")
    deferred_clear = function_body(
        source, "void clearDeferredClientValueEvents("
    )
    deferred_dispatch = function_body(
        source, "void dispatchDeferredUserCallbacks()"
    )
    value_handler = function_body(
        source, "void BLEClientCharacteristic::handleValueEvent("
    )
    reset_discovery = function_body(
        source, "void BLEClientCharacteristic::resetDiscovery()"
    )
    reset_client_state = function_body(
        source, "void resetClientDiscoveryState()"
    )
    connection_edge = function_body(source, "void handleConnectionEdge(bool connected)")
    assert "kDeferredClientValueEventQueueDepth = 8U" in source
    assert "deferred_client_value_event_count_ >=" in deferred_enqueue
    assert "++deferred_client_value_event_count_;" in deferred_enqueue
    assert "deferred_client_value_event_count_ = retained;" in deferred_clear
    assert "while (deferred_client_value_event_count_ > 0U)" in deferred_dispatch
    assert "--deferred_client_value_event_count_;" in deferred_dispatch
    assert "enqueueDeferredClientValueEvent(" in value_handler
    assert "clearDeferredClientValueEvents(this)" in reset_discovery
    assert "connection_generation" in deferred_enqueue
    assert "eventGeneration == connection_generation_" in deferred_dispatch
    assert "discovery_generation_ == eventGeneration" in deferred_dispatch
    assert "client_characteristics_[i]->resetDiscovery();" in reset_client_state
    assert "client_services_[i]->resetDiscovery();" in reset_client_state
    assert "uart->discovered_ = false;" in reset_client_state
    assert "uart->rx_count_ = 0U;" in reset_client_state
    assert connection_edge.count("resetClientDiscoveryState();") == 2
    assert "registerClientService(this)" in function_body(
        source, "bool BLEClientService::begin()"
    )
    assert "service_.discovered() && txd_.discovered()" in header
    assert "pending_notify_callback_" not in source
    assert "pending_notify_callback_" not in header
    print("PASS bounded notifications and connection-generation client state contract")

    discovery = function_body(source, "bool discoverCharacteristicSync(")
    assert "if (found && candidateDecl > *declHandle)" in discovery
    assert "searchStart = static_cast<uint16_t>(lastDecl + 1U);" in discovery
    assert "if (found) {\n      return true;" not in discovery
    print("PASS paged characteristic end-handle discovery contract")

    layout = function_body(
        source, "constexpr bool validAttDiscoveryListLayout("
    )
    assert "entryLength != shortEntryLength && entryLength != longEntryLength" in layout
    assert "kAttDiscoveryListPrefixLength + entryLength" in layout
    assert "(payloadLength - kAttDiscoveryListPrefixLength) % entryLength" in layout
    fixed_layout = function_body(source, "constexpr bool validAttFixedListLayout(")
    assert "prefixLength + entryLength" in fixed_layout
    assert "(payloadLength - prefixLength) % entryLength" in fixed_layout
    service_discovery = function_body(source, "bool discoverServiceRangeSync(")
    assert "validAttFixedListLayout(payloadLength, 5U, 4U)" in service_discovery
    assert "for (uint16_t offset = 5U;" in service_discovery
    assert "validAttDiscoveryListLayout(payloadLength, entryLen, 6U, 20U)" in service_discovery
    assert "for (uint16_t offset = kAttDiscoveryListPrefixLength;" in service_discovery
    assert "offset + entryLen <= payloadLength; offset += entryLen" in service_discovery
    assert "static_cast<uint8_t>(entryLen - 1U)" not in service_discovery
    assert service_discovery.count("validAttServiceGroupEntry(") == 2
    assert "validAttDiscoveryListLayout(payloadLength, entryLen, 7U, 21U)" in discovery
    assert "for (uint16_t offset = kAttDiscoveryListPrefixLength;" in discovery
    assert "offset + entryLen <= payloadLength; offset += entryLen" in discovery
    assert "static_cast<uint8_t>(entryLen - 1U)" not in discovery
    assert "validAttCharacteristicEntry(" in discovery
    assert "lastDecl = static_cast<uint16_t>(searchStart - 1U)" in discovery
    assert "previousValueHandle = candidateValue" in discovery
    for descriptor_signature in (
        "bool discoverCccdHandleSync(",
        "bool discoverReportReferenceHandleSync(",
    ):
        descriptor_discovery = function_body(source, descriptor_signature)
        assert "validAttDiscoveryListLayout(payloadLength, entryLen, 4U, 18U)" in descriptor_discovery
        assert descriptor_discovery.count("validAttOrderedHandle(") == 2
        assert descriptor_discovery.count(
            "for (uint16_t offset = kAttDiscoveryListPrefixLength;"
        ) == 2
        assert "lastHandle = static_cast<uint16_t>(searchStart - 1U)" in descriptor_discovery
        assert "for (uint8_t offset" not in descriptor_discovery
    for malformed_contract in (
        "validAttDiscoveryListLayout(11U, 6U, 6U, 20U)",
        "validAttDiscoveryListLayout(13U, 6U, 6U, 20U)",
        "validAttDiscoveryListLayout(14U, 8U, 6U, 20U)",
        "validAttDiscoveryListLayout(12U, 7U, 7U, 21U)",
        "validAttDiscoveryListLayout(14U, 7U, 7U, 21U)",
        "validAttDiscoveryListLayout(14U, 8U, 7U, 21U)",
    ):
        assert f"static_assert(!{malformed_contract}" in source
    for malformed_handle_contract in (
        "validAttServiceGroupEntry(4U, 3U, 1U, 0xFFFFU, 0U)",
        "validAttServiceGroupEntry(5U, 8U, 6U, 0xFFFFU, 5U)",
        "validAttCharacteristicEntry(2U, 2U, 1U, 10U, 0U)",
        "validAttCharacteristicEntry(5U, 6U, 6U, 10U, 5U)",
        "validAttCharacteristicEntry(3U, 4U, 1U, 10U, 3U)",
        "validAttOrderedHandle(5U, 6U, 10U, 5U)",
    ):
        assert f"static_assert(!{malformed_handle_contract}" in source
    for malformed_fixed_layout in (
        "validAttFixedListLayout(8U, 5U, 4U)",
        "validAttFixedListLayout(10U, 5U, 4U)",
    ):
        assert f"static_assert(!{malformed_fixed_layout}" in source
    print(
        "PASS malformed ATT discovery layout, handle range, and pagination "
        "rejection contracts"
    )

    hid_discovery = function_body(
        source, "bool BLEClientHidAdafruit::discoverGamepadReport()"
    )
    assert "discoverReportReferenceHandleSync" in hid_discovery
    assert "readHandleSync(reportReferenceHandle" in hid_discovery
    assert "reportReference[0] == 1U" in hid_discovery
    assert "reportReference[1] == 1U" in hid_discovery
    assert "!keyboardPresent() && !mousePresent()" in hid_discovery
    assert "gamepad_report_.value_handle_ = valueHandle;" in hid_discovery
    assert "friend class BLEClientHidAdafruit;" in header
    print("PASS HID gamepad Report Reference selection contract")

    for signature in (
        "bool BLEHidAdafruit::keyboardReport(uint16_t conn_hdl, hid_keyboard_report_t* report)",
        "bool BLEHidAdafruit::consumerReport(uint16_t conn_hdl, uint16_t usage_code)",
        "bool BLEHidAdafruit::mouseReport(uint16_t conn_hdl, hid_mouse_report_t* report)",
        "bool BLEHidGamepad::report(uint16_t conn_hdl, hid_gamepad_report_t* report)",
    ):
        assert "hidLinkEncrypted(conn_hdl)" in function_body(source, signature)
    print("PASS encrypted-only HID notification contract")

    mapping = function_body(source, "uint8_t mapProperties(uint8_t properties)")
    assert "CHR_PROPS_AUTH_SIGNED_WRITES" in mapping
    assert "kBleGattPropAuthenticatedSignedWrites" in mapping
    signed_write = function_body(
        source,
        "uint16_t BLEClientCharacteristic::writeSigned(const void* buffer, uint16_t len)",
    )
    assert "len > 240U" in signed_write
    assert "15U + len" in signed_write
    assert "queueAttSignedWriteCommand" in signed_write
    assert "writeSigned(const void* buffer, uint16_t len);" in header
    print("PASS authenticated signed-write Bluefruit contract")

    dfu_begin = function_body(source, "err_t BLEDfu::begin()")
    assert "return ERROR_NOT_SUPPORTED;" in dfu_begin
    assert "ERROR_NONE" not in dfu_begin
    assert "_begun" not in dfu_begin
    examples = BLUEFRUIT / "examples"
    for sketch in examples.rglob("*.ino"):
        contents = sketch.read_text(encoding="utf-8")
        assert "BLEDfu" not in contents, f"unsupported BLEDfu used by {sketch}"
        assert "bledfu.begin" not in contents, f"unsupported DFU started by {sketch}"
    print("PASS unsupported DFU fails explicitly and examples do not advertise it")

    battery_write = function_body(source, "bool BLEBas::write(uint8_t level)")
    battery_notify = function_body(
        source, "bool BLEBas::notify(uint16_t conn_hdl, uint8_t level)"
    )
    assert "writeGattBatteryLevel(level)" in battery_write
    assert "notifyGattBatteryLevel(level)" in battery_notify
    assert "conn_hdl != 0U" in battery_notify
    assert "return write(level)" not in battery_notify
    hal_core = (
        ROOT
        / "hardware/nrf54l15clean/nrf54l15clean/libraries/"
        "Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/"
        "nrf54l15_hal_ble_core_setup.inc"
    ).read_text(encoding="utf-8")
    battery_set = function_body(
        hal_core, "bool BleRadio::writeGattBatteryLevel(uint8_t percent)"
    )
    battery_queue = function_body(
        hal_core, "bool BleRadio::notifyGattBatteryLevel(uint8_t percent)"
    )
    assert "connectionBatteryNotificationPending_" not in battery_set
    assert "!connected_ || !connectionBatteryNotificationsEnabled_" in battery_queue
    assert "connectionBatteryNotificationPending_ = true;" in battery_queue
    print("PASS Battery Service write/notify separation contract")

    uart_buffer = function_body(source, "void BLEUart::bufferTXD(bool enable)")
    uart_flush = function_body(
        source, "bool BLEUart::flushTXD(uint16_t conn_hdl)"
    )
    uart_write = function_body(
        source,
        "size_t BLEUart::write(uint16_t conn_hdl, const uint8_t* content, size_t len)",
    )
    uart_stream_flush = function_body(source, "void BLEUart::flush()")
    uart_cccd = function_body(source, "void BLEUart::bleuart_txd_cccd_cb(")
    client_uart_write = function_body(
        source, "size_t BLEClientUart::write(const uint8_t* buffer, size_t size)"
    )
    assert "new uint8_t[BLUEFRUIT_GATT_VALUE_MAX_LEN]" in uart_buffer
    assert "_tx_buffered = enable && _tx_fifo != nullptr;" in uart_buffer
    assert "while (_tx_fifo_count > 0U)" in uart_flush
    assert "_txd.notify(conn_hdl, _tx_fifo, chunk)" in uart_flush
    assert "memmove(_tx_fifo, &_tx_fifo[chunk], _tx_fifo_count)" in uart_flush
    assert "stalledAtMs" in uart_flush
    assert "yield();" in uart_flush
    assert "if (_tx_buffered && _tx_fifo != nullptr)" in uart_write
    assert "_tx_fifo_count == packetCapacity && !flushTXD(conn_hdl)" in uart_write
    assert "min<size_t>" in uart_write
    assert "static_cast<uint16_t>(len - sent)" not in uart_write
    assert "service._tx_fifo_count = 0U;" in uart_cccd
    assert "if (!enabled)" in uart_cccd
    assert "min<size_t>" in client_uart_write
    assert "rxd_.write(&buffer[sent], chunk, true) != chunk" in client_uart_write
    assert "const uint16_t chunk" in client_uart_write
    assert "static_cast<uint16_t>(size - sent)" not in client_uart_write
    assert "_rx_count = 0U;" in uart_stream_flush
    assert "~BLEUart() override;" in header
    print("PASS BLE UART generation, backpressure, and large-write contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
