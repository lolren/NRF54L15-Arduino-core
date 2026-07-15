#!/usr/bin/env python3
"""Deterministic contracts for BLE connection scheduling and TX reliability."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PARTS = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries/"
    "Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts"
)
SCANNING = PARTS / "nrf54l15_hal_ble_scanning_connections.inc"
EVENT_RX = PARTS / "nrf54l15_hal_ble_peripheral_event_rx.inc"
EVENT_TX = PARTS / "nrf54l15_hal_ble_peripheral_event_tx.inc"
EVENT_TAIL = PARTS / "nrf54l15_hal_ble_peripheral_event_tail.inc"
SECURITY = PARTS / "nrf54l15_hal_ble_ll_security.inc"
CUSTOM_GATT = PARTS / "nrf54l15_hal_ble_custom_gatt.inc"
CENTRAL_EVENT = PARTS / "nrf54l15_hal_ble_central_event.inc"
CONNECTION_API = PARTS / "nrf54l15_hal_ble_connection_api.inc"
BLUEFRUIT = (
    ROOT
    / "hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/bluefruit.cpp"
)


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    assert start >= 0, f"missing function: {signature}"
    brace = text.find("{", start + len(signature))
    assert brace >= 0, f"missing opening brace: {signature}"
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


@dataclass
class PeripheralSchedule:
    """Small CSA#1 schedule model using the controller's counter convention."""

    connect_ind_end_us: int
    window_offset: int
    window_size: int
    interval_units: int
    hop: int
    event_counter: int = 0

    @property
    def anchor_us(self) -> int:
        return self.connect_ind_end_us + 1250 + (self.window_offset * 1250)

    @property
    def window_close_us(self) -> int:
        return self.anchor_us + (self.window_size * 1250)

    def channel_for_current_event(self) -> int:
        return (self.hop * (self.event_counter + 1)) % 37

    def consume_event(self) -> tuple[int, int]:
        event = self.event_counter
        channel = self.channel_for_current_event()
        self.event_counter += 1
        return event, channel


@dataclass
class ConnectionUpdateInstantSchedule:
    """Connection-update timing using the controller's pre-advanced counter."""

    next_event_us: int
    old_interval_units: int
    new_interval_units: int
    win_offset_units: int
    event_counter: int

    @property
    def old_interval_us(self) -> int:
        return self.old_interval_units * 1250

    @property
    def new_interval_us(self) -> int:
        return self.new_interval_units * 1250

    def apply_at_instant(self) -> tuple[int, int]:
        current_counter = self.event_counter
        current_anchor_us = self.next_event_us

        # Normal event entry advances once on the old cadence before applying
        # an LL instant. The update then replaces that old-cadence timestamp,
        # but must retain the already-advanced following-event counter.
        self.event_counter = (self.event_counter + 1) & 0xFFFF
        self.next_event_us += self.old_interval_us
        instant_window_start_us = (
            current_anchor_us + self.win_offset_units * 1250
        )
        self.next_event_us = instant_window_start_us + self.new_interval_us
        return current_counter, current_anchor_us

    def consume_first_new_event(self) -> tuple[int, int]:
        current_counter = self.event_counter
        current_anchor_us = self.next_event_us
        self.event_counter = (self.event_counter + 1) & 0xFFFF
        self.next_event_us += self.new_interval_us
        return current_counter, current_anchor_us


@dataclass
class MissedConnectionUpdateSchedule:
    """Late-skip behavior while approaching a pending update instant."""

    next_event_us: int
    current_interval_units: int
    new_interval_units: int
    win_offset_units: int
    event_counter: int
    update_instant: int
    update_pending: bool = True

    def late_skip(self, generic_catch_up_steps: int = 2) -> tuple[int, int, int, bool]:
        skipped_counter = self.event_counter
        skipped_anchor_us = self.next_event_us
        skipped_instant = (
            self.update_pending and skipped_counter == self.update_instant
        )
        next_is_instant = (
            self.update_pending
            and ((skipped_counter + 1) & 0xFFFF) == self.update_instant
        )
        advance_count = (
            1 if skipped_instant or next_is_instant else generic_catch_up_steps
        )
        interval_us = self.current_interval_units * 1250
        for _ in range(advance_count):
            self.next_event_us += interval_us
            self.event_counter = (self.event_counter + 1) & 0xFFFF

        if skipped_instant:
            self.current_interval_units = self.new_interval_units
            instant_window_start_us = (
                skipped_anchor_us + self.win_offset_units * 1250
            )
            self.next_event_us = (
                instant_window_start_us + self.new_interval_units * 1250
            )
            self.update_pending = False
        return (
            skipped_counter,
            skipped_anchor_us,
            advance_count,
            skipped_instant,
        )


def supervision_event_threshold(timeout_us: int, interval_us: int) -> int:
    assert timeout_us >= 0
    assert interval_us > 0
    return max(1, (timeout_us + interval_us - 1) // interval_us)


class PendingKind(Enum):
    NONE = auto()
    SERVER_PUSH = auto()
    ATT_RESPONSE = auto()
    LL_CONTROL = auto()
    SMP = auto()


@dataclass
class PendingTxPriorityModel:
    pending: PendingKind = PendingKind.NONE
    server_push_preservable: bool = False
    preserved_server_pushes: int = 0

    def queue_smp(self) -> bool:
        if self.pending is PendingKind.NONE:
            self.pending = PendingKind.SMP
            return True
        if self.pending is PendingKind.SERVER_PUSH and self.server_push_preservable:
            self.preserved_server_pushes += 1
            self.pending = PendingKind.SMP
            return True
        return False

    def acknowledge_pending(self) -> None:
        assert self.pending is not PendingKind.NONE
        self.pending = PendingKind.NONE
        if self.preserved_server_pushes:
            self.preserved_server_pushes -= 1
            self.pending = PendingKind.SERVER_PUSH


def validate_schedule_model() -> None:
    schedule = PeripheralSchedule(
        connect_ind_end_us=100_000,
        window_offset=3,
        window_size=2,
        interval_units=24,
        hop=5,
    )
    assert schedule.anchor_us == 105_000
    assert schedule.window_close_us == 107_500

    # Event zero uses the first hop after CONNECT_IND. Missing that complete
    # window consumes exactly event zero; the following poll must use event one
    # and its channel rather than listening again on event zero's channel.
    assert schedule.consume_event() == (0, 5)
    assert schedule.consume_event() == (1, 10)
    assert schedule.consume_event() == (2, 15)

    wrap = PeripheralSchedule(0xFFFF_FF00, 0, 1, 24, 16)
    assert wrap.consume_event() == (0, 16)
    assert wrap.consume_event() == (1, 32)
    assert wrap.consume_event() == (2, 11)
    print("PASS first-anchor, transmit-window, and channel-advance model")


def validate_connection_update_instant_model() -> None:
    moved = ConnectionUpdateInstantSchedule(
        next_event_us=1_000_000,
        old_interval_units=24,
        new_interval_units=12,
        win_offset_units=3,
        event_counter=0x1234,
    )
    assert moved.apply_at_instant() == (0x1234, 1_000_000)
    assert moved.event_counter == 0x1235
    assert moved.next_event_us == 1_018_750
    assert moved.next_event_us != 1_000_000 + moved.old_interval_us
    assert moved.consume_first_new_event() == (0x1235, 1_018_750)
    assert moved.event_counter == 0x1236
    assert moved.next_event_us == 1_033_750

    # WinOffset zero starts the instant window at the old-cadence instant.
    # The first following event is exactly one new interval later and uses the
    # immediately following event counter.
    zero_offset = ConnectionUpdateInstantSchedule(
        next_event_us=2_000_000,
        old_interval_units=40,
        new_interval_units=8,
        win_offset_units=0,
        event_counter=0x0200,
    )
    assert zero_offset.apply_at_instant() == (0x0200, 2_000_000)
    assert zero_offset.event_counter == 0x0201
    assert zero_offset.next_event_us == 2_010_000
    assert zero_offset.consume_first_new_event() == (0x0201, 2_010_000)
    assert zero_offset.event_counter == 0x0202
    assert zero_offset.next_event_us == 2_020_000
    print("PASS connection-update instant timing and counter model")


def validate_missed_connection_update_instant_model() -> None:
    missed = MissedConnectionUpdateSchedule(
        next_event_us=3_000_000,
        current_interval_units=24,
        new_interval_units=12,
        win_offset_units=2,
        event_counter=0x0300,
        update_instant=0x0301,
    )

    # Although a generic late catch-up would take two old-cadence steps, the
    # event immediately before the instant must stop exactly on the instant.
    assert missed.late_skip(generic_catch_up_steps=2) == (
        0x0300,
        3_000_000,
        1,
        False,
    )
    assert missed.event_counter == 0x0301
    assert missed.next_event_us == 3_030_000
    assert missed.update_pending

    # Skipping the instant takes one more old-cadence step only to advance the
    # counter. Applying the update replaces that provisional timestamp with
    # instant-window-start + one new interval; there is no second old step.
    assert missed.late_skip(generic_catch_up_steps=2) == (
        0x0301,
        3_030_000,
        1,
        True,
    )
    assert missed.event_counter == 0x0302
    assert missed.next_event_us == 3_047_500
    assert missed.current_interval_units == 12
    assert not missed.update_pending
    print("PASS missed connection-update instant one-step catch-up model")


def validate_supervision_threshold_model() -> None:
    assert supervision_event_threshold(5_000_000, 7_500) == 667
    assert supervision_event_threshold(5_000_000, 10_000) == 500
    assert supervision_event_threshold(1, 7_500) == 1
    print("PASS ceiling-based peripheral supervision threshold model")


def validate_schedule_source_contract() -> None:
    scanning = SCANNING.read_text(encoding="utf-8")
    peripheral_event = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (EVENT_RX, EVENT_TX, EVENT_TAIL)
    )
    security = SECURITY.read_text(encoding="utf-8")

    connect = function_body(scanning, "bool BleRadio::startConnectionFromConnectInd(")
    required_connect = (
        "connectionEventCounter_ = 0U;",
        "bleLlInitialSyncWindowUs(winSize)",
        "1250UL + (static_cast<uint32_t>(winOffset) * 1250UL)",
        "connectionNextEventUs_ = nowUs +",
    )
    for needle in required_connect:
        assert needle in connect, f"first-anchor setup lost: {needle}"
    assert connect.index("connectionEventCounter_ = 0U;") < connect.index(
        "connectionNextEventUs_ = nowUs +"
    )

    poll = function_body(
        peripheral_event, "bool BleRadio::pollConnectionEventInternal("
    )
    late = poll.index("if (useCurrentEventCounterForChannel)")
    skip_update = poll.index("updateNextConnectionEventTime();", late)
    skip_return = poll.index("return false;", skip_update)
    select = poll.index("selectNextDataChannel(useCurrentEventCounterForChannel)")
    assert late < skip_update < skip_return < select, (
        "a fully missed first window must advance the event exactly once and "
        "return before selecting/listening on a stale channel"
    )
    regular_update = poll.index("updateNextConnectionEventTime();", skip_return)
    current_counter = poll.index("const uint16_t currentEventCounter", regular_update)
    assert regular_update < current_counter < select
    assert "connectionEventCounter_ - 1U" in poll[current_counter:select]
    assert "if (rxListenUs > 50000U)" not in poll, (
        "a legal CONNECT_IND or connection-update transmit window must not be "
        "truncated to 50 ms"
    )

    selector = function_body(security, "uint8_t BleRadio::selectNextDataChannel(")
    assert "bleCsa2PrnE(eventCounter, connectionChannelId_)" in selector
    assert "static_cast<uint32_t>(eventCounter + 1U)" in selector
    print("PASS production first-anchor and channel-counter source contract")


def validate_connection_update_instant_source_contract() -> None:
    custom_gatt = CUSTOM_GATT.read_text(encoding="utf-8")
    update = function_body(
        custom_gatt, "void BleRadio::applyPendingConnectionUpdateAtInstant("
    )

    required_update = (
        "connectionIntervalUnits_ = connectionPendingIntervalUnits_;",
        "static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL",
        "static_cast<uint32_t>(connectionPendingWinOffset_) * 1250UL",
        "const uint32_t instantWindowStartUs =",
        "currentEventAnchorUs + winOffsetUs",
        "connectionNextEventUs_ = instantWindowStartUs + newIntervalUs;",
    )
    for needle in required_update:
        assert needle in update, (
            f"connection-update instant contract missing: {needle}"
        )

    interval_apply = update.index(
        "connectionIntervalUnits_ = connectionPendingIntervalUnits_;"
    )
    window_start = update.index("const uint32_t instantWindowStartUs =")
    following_event = update.index(
        "connectionNextEventUs_ = instantWindowStartUs + newIntervalUs;"
    )
    catch_up = update.index("while (guard--", following_event)
    assert interval_apply < window_start < following_event < catch_up
    assert "++connectionEventCounter_" not in update[:catch_up], (
        "the normal event-entry advance already selected the first following "
        "counter; applying the instant must not advance it again"
    )
    assert update.count("++connectionEventCounter_") == 1
    late_timestamp_advance = update.index(
        "connectionNextEventUs_ += newIntervalUs;", catch_up
    )
    late_counter_advance = update.index("++connectionEventCounter_;", catch_up)
    assert catch_up < late_timestamp_advance < late_counter_advance, (
        "only genuinely elapsed post-update events may advance the timestamp "
        "and counter beyond the first following event"
    )

    # Both connection roles snapshot the instant's old-cadence anchor, perform
    # their one normal event-entry advance, derive the current counter as N-1,
    # and only then replace the following-event schedule at the instant.
    peripheral_event = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (EVENT_RX, EVENT_TX, EVENT_TAIL)
    )
    for source, signature, role in (
        (
            peripheral_event,
            "bool BleRadio::pollConnectionEventInternal(",
            "peripheral",
        ),
        (
            CENTRAL_EVENT.read_text(encoding="utf-8"),
            "bool BleRadio::pollCentralConnectionEvent(",
            "central",
        ),
    ):
        caller = function_body(source, signature)
        anchor_snapshot = caller.index(
            "const uint32_t currentEventAnchorUs = connectionNextEventUs_;"
        )
        normal_advance = caller.index(
            "updateNextConnectionEventTime();", anchor_snapshot
        )
        current_counter = caller.index(
            "const uint16_t currentEventCounter", normal_advance
        )
        apply_update = caller.index(
            "applyPendingConnectionUpdateAtInstant(currentEventCounter, "
            "currentEventAnchorUs);",
            current_counter,
        )
        channel_select = caller.index("selectNextDataChannel(", apply_update)
        assert (
            anchor_snapshot
            < normal_advance
            < current_counter
            < apply_update
            < channel_select
        ), f"{role} connection-update instant call order lost"
        assert "connectionEventCounter_ - 1U" in caller[
            current_counter:apply_update
        ], f"{role} instant counter is not aligned to the pre-advanced event"

    peripheral = function_body(
        peripheral_event, "bool BleRadio::pollConnectionEventInternal("
    )
    late_start = peripheral.index("if (useCurrentEventCounterForChannel)")
    late_end = peripheral.index('emitBleTrace("EVT_LATE_SKIP");', late_start)
    late_skip = peripheral[late_start:late_end]
    required_late_skip = (
        "const uint16_t skippedEventCounter = connectionEventCounter_;",
        "const uint32_t skippedEventAnchorUs = connectionNextEventUs_;",
        "skippedEventCounter == connectionUpdateInstant_",
        "static_cast<uint16_t>(skippedEventCounter + 1U)",
        "if (skippedConnectionUpdateInstant ||",
        "connectionNextEventUs_ += intervalUs;",
        "++connectionEventCounter_;",
        "if (skippedConnectionUpdateInstant)",
        "applyPendingConnectionUpdateAtInstant(skippedEventCounter,",
        "skippedEventAnchorUs);",
        "LL_CONN_UPDATE_INSTANT_SKIPPED",
    )
    for needle in required_late_skip:
        assert needle in late_skip, (
            f"missed connection-update instant contract missing: {needle}"
        )

    counter_snapshot = late_skip.index("const uint16_t skippedEventCounter")
    anchor_snapshot = late_skip.index("const uint32_t skippedEventAnchorUs")
    instant_guard = late_skip.index("if (skippedConnectionUpdateInstant ||")
    one_timestamp_advance = late_skip.index(
        "connectionNextEventUs_ += intervalUs;", instant_guard
    )
    one_counter_advance = late_skip.index(
        "++connectionEventCounter_;", one_timestamp_advance
    )
    apply_skipped = late_skip.index(
        "applyPendingConnectionUpdateAtInstant(skippedEventCounter,",
        one_counter_advance,
    )
    generic_update = late_skip.index(
        "updateNextConnectionEventTime();", apply_skipped
    )
    assert (
        counter_snapshot
        < anchor_snapshot
        < instant_guard
        < one_timestamp_advance
        < one_counter_advance
        < apply_skipped
        < generic_update
    )
    guarded_walk = late_skip[instant_guard:generic_update]
    assert guarded_walk.count("connectionNextEventUs_ += intervalUs;") == 1
    assert guarded_walk.count("++connectionEventCounter_;") == 1
    assert "updateNextConnectionEventTime();" not in guarded_walk
    assert "catchUpConnectionEventTime();" not in guarded_walk
    assert late_skip.count("updateNextConnectionEventTime();") == 1, (
        "generic old-cadence catch-up must remain only in the non-instant branch"
    )

    print("PASS production connection-update instant scheduling source contract")


def validate_peripheral_supervision_source_contract() -> None:
    event_rx = EVENT_RX.read_text(encoding="utf-8")
    ceiling_threshold = "(timeoutUs + intervalUs - 1U) / intervalUs;"
    assert event_rx.count(ceiling_threshold) == 3, (
        "every peripheral timeout path must round the supervision event count up"
    )
    assert "timeoutUs / intervalUs" not in event_rx, (
        "floor division disconnects before a non-integral supervision timeout"
    )
    print("PASS production peripheral supervision ceiling source contract")


def validate_central_catch_up_source_contract() -> None:
    central = CENTRAL_EVENT.read_text(encoding="utf-8")
    security = SECURITY.read_text(encoding="utf-8")
    poll = function_body(central, "bool BleRadio::pollCentralConnectionEvent(")
    catch_up = function_body(security, "void BleRadio::catchUpConnectionEventTime()")

    ceiling_threshold = "(timeoutUs + intervalUs - 1U) / intervalUs;"
    assert ceiling_threshold in poll
    assert "timeoutUs / intervalUs" not in poll
    for token in (
        "connectionUpdatePending_",
        "llEventInstantReached(connectionEventCounter_,",
        "connectionUpdateInstant_",
        "applyPendingConnectionUpdateAtInstant(skippedInstant,",
        "skippedAnchorUs);",
        "LL_CONN_UPDATE_INSTANT_CATCHUP",
    ):
        assert token in catch_up, (
            f"central late catch-up can skip a connection-update instant: {token}"
        )
    print("PASS central supervision and instant-aware catch-up source contract")


def validate_central_encryption_handshake_source_contract() -> None:
    central = CENTRAL_EVENT.read_text(encoding="utf-8")
    security = SECURITY.read_text(encoding="utf-8")
    enc_response = security[
        security.index("case kBleLlCtrlEncRsp:") :
        security.index("case kBleLlCtrlStartEncRsp:")
    ]
    assert "BleLlEncryptionPhase::kAwaitStartRequest" in enc_response
    assert "BleLlEncryptionPhase::kSendStartRequest" not in enc_response
    for token in (
        "const bool txStartsEncryption =",
        "connectionEncEnableTxOnNextEvent_",
        "connectionTxPayload_[0] == kBleLlCtrlStartEncRsp",
        "connectionEncTxEnabled_ || txStartsEncryption",
        "CENTRAL_START_ENC_RSP_TX",
        "connectionEncAwaitingStartRsp_ = true",
    ):
        assert token in central, (
            f"central three-way encryption handshake missing: {token}"
        )
    print("PASS central waits for peripheral START_ENC_REQ and encrypts START_ENC_RSP")


def validate_graceful_local_disconnect_source_contract() -> None:
    connection = CONNECTION_API.read_text(encoding="utf-8")
    event_rx = EVENT_RX.read_text(encoding="utf-8")
    event_tx = EVENT_TX.read_text(encoding="utf-8")
    event_tail = EVENT_TAIL.read_text(encoding="utf-8")
    central = CENTRAL_EVENT.read_text(encoding="utf-8")
    bluefruit = BLUEFRUIT.read_text(encoding="utf-8")

    request = function_body(connection, "bool BleRadio::requestDisconnect(")
    assert "connectionLocalTerminatePending_ = true;" in request
    assert "manager().radio().requestDisconnect()" in bluefruit
    for token in (
        "connectionLocalTerminatePending_",
        "connectionTxHistoryValid_ || peerAckedLastTx",
        "kBleLlCtrlTerminateInd",
        "terminateLocalErrorCode",
        "LOCAL_TERMINATE_TX",
    ):
        assert token in event_tx, f"peripheral graceful disconnect missing: {token}"
    assert "terminateLocalRequest && !txOk" in event_tail
    assert "LOCAL_TERMINATE_SENT" in event_tail
    for token in (
        "connectionLocalTerminatePending_",
        "kBleLlCtrlTerminateInd",
        "CENTRAL_LOCAL_TERMINATE_TX",
        "CENTRAL_LOCAL_TERMINATE_SENT",
    ):
        assert token in central, f"central graceful disconnect missing: {token}"
    assert "bool terminateLocalRequest = false;" in event_rx
    print("PASS graceful local LL_TERMINATE_IND source contract")


def validate_pending_smp_model() -> None:
    empty = PendingTxPriorityModel()
    assert empty.queue_smp() and empty.pending is PendingKind.SMP

    notification = PendingTxPriorityModel(PendingKind.SERVER_PUSH, True)
    assert notification.queue_smp()
    assert notification.pending is PendingKind.SMP
    assert notification.preserved_server_pushes == 1
    notification.acknowledge_pending()
    assert notification.pending is PendingKind.SERVER_PUSH

    # SMP cannot overwrite an older protocol response, LL control procedure,
    # unpreservable notification, or an already queued SMP PDU.
    for kind, preservable in (
        (PendingKind.SERVER_PUSH, False),
        (PendingKind.ATT_RESPONSE, False),
        (PendingKind.LL_CONTROL, False),
        (PendingKind.SMP, False),
    ):
        blocked = PendingTxPriorityModel(kind, preservable)
        assert not blocked.queue_smp()
        assert blocked.pending is kind
        assert blocked.preserved_server_pushes == 0
    print("PASS high-priority SMP displacement/preservation model")


def validate_pending_smp_source_contract() -> None:
    security = SECURITY.read_text(encoding="utf-8")
    event_tx = EVENT_TX.read_text(encoding="utf-8")
    queue = function_body(security, "bool BleRadio::queuePendingSmpL2capResponse(")

    # Deferred DHKey-check and Pairing Failed responses are link-progress
    # traffic. They may displace a preservable notification/indication, but
    # must never silently discard it or overwrite another protocol response.
    assert "if (!connected_ || connectionPendingTxValid_)" not in queue, (
        "queued application notifications currently block deferred SMP; "
        "classify/preserve server pushes before rejecting an occupied slot"
    )
    required_queue = (
        "connectionPendingTxValid_",
        "pendingTxIsAttServerPushPdu",
        "canPreservePendingServerPushPdu",
        "preservePendingServerPushPdu",
        "buildSmpL2capResponse",
        "connectionPendingTxLlid_ = kBlePduDataStartOrComplete",
        "connectionPendingTxValid_ = true",
    )
    for needle in required_queue:
        assert needle in queue, f"SMP priority queue contract missing: {needle}"

    # The normal event selector must drain a queued SMP before lower-priority
    # server pushes, while Link Layer history still controls retransmission.
    fresh_selection = event_tx.index("if (txCanUseFreshPayload)")
    pending_selection = event_tx.index(
        "if (connectionPendingTxValid_", fresh_selection
    )
    assert fresh_selection < pending_selection
    assert "SMP_SC_DHKEY_CHECK_TXQ" in event_tx
    print("PASS production high-priority SMP TX source contract")


def validate_fast_connection_paths() -> None:
    event_rx = EVENT_RX.read_text(encoding="utf-8")
    event_tx = EVENT_TX.read_text(encoding="utf-8")
    fast_ack = function_body(
        event_rx, "auto tryFastEncryptedPacketAck = [&]() -> bool"
    )
    for pending_work in (
        "connectionPendingTxValid_",
        "connectionPendingL2capTxFragmentActive_",
        "connectionGattAuthorization_.responseReady",
        "connectionCustomNotificationQueueCount_ > 0U",
    ):
        assert pending_work in fast_ack, (
            f"encrypted fast ACK can starve queued work: {pending_work}"
        )
    for timing_token in (
        "const uint32_t codedRxEndAdjustUs =",
        "? 256U : 0U",
        "rxEndTimestampUs + codedRxEndAdjustUs + kBleConnTxenAfterRxUs",
    ):
        assert timing_token in fast_ack, (
            f"generic coded-PHY fast ACK timing missing: {timing_token}"
        )
    for history_token in (
        "plainVersionInd && connectionTxHistoryValid_",
        "!peerAckedLastTxEarly",
        "return false;",
    ):
        assert history_token in fast_ack, (
            "LL_VERSION_IND may replace unacknowledged TX history: "
            f"{history_token}"
        )
    for lifecycle_token in (
        "queueNextPendingL2capTxFragment()",
        "connectionSyncAttemptsRemaining_",
        "PEER_TERMINATE_FAST",
        "BleDisconnectReason::kPeerTerminate",
        "event->disconnectReasonRemote = true",
    ):
        assert lifecycle_token in fast_ack, (
            f"encrypted fast path lost lifecycle handling: {lifecycle_token}"
        )

    # Empty LLID1 PDUs are not encrypted even while link encryption is active.
    # They carry no MIC and consume no CCM receive counter.
    for empty_rx_token in (
        "const bool encryptedEmptyPdu =",
        "llidEarly == kBlePduDataContinuation",
        "rxLengthEarly == 0U",
        "!encryptedEmptyPdu &&",
        "encryptedEmptyPdu\n                   ? true",
        "encryptedPacket && !encryptedEmptyPdu && packetIsNewEarly",
    ):
        assert empty_rx_token in fast_ack, (
            f"encrypted-session empty RX PDU contract missing: {empty_rx_token}"
        )

    fast_dhkey = function_body(
        event_rx,
        "auto tryFastPlaintextDhKeyCheckRetransmission = [&]() -> bool",
    )
    for retry_token in (
        "connectionLastTxPlainLength_",
        "kSmpCodeDhKeyCheck",
        "snEarly != connectionExpectedRxSn_",
        "nesnEarly == connectionLastTxSn_",
        "SMP_SC_DHKEY_CHECK_FAST_RETX",
    ):
        assert retry_token in fast_dhkey, (
            f"DHKey retransmission path missing: {retry_token}"
        )
    assert "incomingDhKeyCheck" not in fast_dhkey, (
        "the peer may poll with an empty duplicate while our DHKey check is "
        "unacknowledged; retry must be selected from cached TX history"
    )

    fast_encrypted_retry = function_body(
        event_rx,
        "auto tryFastEncryptedCachedRetransmission = [&]() -> bool",
    )
    for retry_token in (
        "snEarly != connectionExpectedRxSn_",
        "nesnEarly == connectionLastTxSn_",
        "connectionTxHistoryValid_",
        "connectionLastTxWasEncrypted_",
        "connectionLastTxEncryptedLength_",
        "memcpy(&txPacket_[2], connectionLastTxEncryptedPayload_, txAirLength)",
        "--rxCounter",
        "bleCcmDecryptPayload",
        "ENC_CACHED_FAST_RETX",
        "event->packetIsNew = false",
        "event->peerAckedLastTx = false",
        "event->freshTxAllowed = false",
    ):
        assert retry_token in fast_encrypted_retry, (
            f"encrypted cached retransmission path missing: {retry_token}"
        )

    # Empty LLID1 acknowledgements remain unencrypted on an encrypted link.
    # They still occupy TX history and must be eligible for the same TIFS retry
    # without copying stale encrypted bytes into the radio packet.
    for empty_ack_token in (
        "const bool cachedPlainEmptyAck =",
        "!connectionLastTxWasEncrypted_",
        "connectionLastTxPlainLlid_ == kBlePduDataContinuation",
        "connectionLastTxPlainLength_ == 0U",
        "(!cachedEncryptedPdu && !cachedPlainEmptyAck)",
        "cachedEncryptedPdu ? connectionLastTxEncryptedLength_ : 0U",
        "if (txAirLength > 0U)",
        "ENC_EMPTY_ACK_FAST_RETX",
    ):
        assert empty_ack_token in fast_encrypted_retry, (
            f"encrypted-session empty ACK retry missing: {empty_ack_token}"
        )

    # A duplicate LLID1 packet can either be a zero-length plaintext ACK or an
    # encrypted continuation carrying at least its MIC. The latter must use the
    # previous receive counter just like duplicate LLID2/LLID3 packets.
    for continuation_auth_token in (
        "llidEarly == kBlePduDataContinuation && rxLengthEarly == 0U",
        "llidEarly == kBlePduDataContinuation ||",
        "rxLengthEarly >= kBleMicLen",
        "--rxCounter",
        "bleCcmDecryptPayload",
    ):
        assert continuation_auth_token in fast_encrypted_retry, (
            "LLID1 duplicate authentication contract missing: "
            f"{continuation_auth_token}"
        )

    for coded_timing_token in (
        "(connectionCurrentRxPhy_ & kBlePhyCoded) != 0U",
        "? 256U : 0U",
        "rxEndTimestampUs + codedRxEndAdjustUs + kBleConnTxenAfterRxUs",
    ):
        assert coded_timing_token in fast_encrypted_retry, (
            f"coded-PHY fast retry timing adjustment missing: {coded_timing_token}"
        )

    for forbidden_mutation in (
        "bleCcmEncryptPayload",
        "connectionExpectedRxSn_ ^=",
        "connectionLastTxSn_ ^=",
    ):
        assert forbidden_mutation not in fast_encrypted_retry, (
            "an encrypted retransmission must reuse the original ciphertext, "
            f"packet counters, and sequence state: {forbidden_mutation}"
        )
    assert "connectionEncTxCounter_" not in fast_encrypted_retry, (
        "reusing cached ciphertext must not read or advance the TX counter"
    )
    assert fast_encrypted_retry.count("connectionEncRxCounter_") == 1, (
        "a duplicate may snapshot the RX counter to authenticate with counter-1, "
        "but must not advance the receive counter"
    )

    encrypted_retry_call = event_rx.index(
        "if (tryFastEncryptedCachedRetransmission())"
    )
    generic_fast_ack = event_rx.index(
        "auto tryFastEncryptedPacketAck = [&]() -> bool"
    )
    assert encrypted_retry_call < generic_fast_ack, (
        "an unacknowledged encrypted response must be retried before the "
        "generic fast ACK path can replace TX history"
    )

    assert "else if (rxL2capCid != kBleL2capCidSmp)" in event_tx, (
        "controller-owned SMP deferred work must not be consumed twice"
    )
    print(
        "PASS fast ACK, peer-terminate, DHKey, and encrypted retry "
        "source contracts"
    )


def main() -> int:
    validate_schedule_model()
    validate_connection_update_instant_model()
    validate_missed_connection_update_instant_model()
    validate_supervision_threshold_model()
    validate_schedule_source_contract()
    validate_connection_update_instant_source_contract()
    validate_peripheral_supervision_source_contract()
    validate_central_catch_up_source_contract()
    validate_central_encryption_handshake_source_contract()
    validate_graceful_local_disconnect_source_contract()
    validate_pending_smp_model()
    validate_pending_smp_source_contract()
    validate_fast_connection_paths()
    print("PASS all BLE connection regression contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
