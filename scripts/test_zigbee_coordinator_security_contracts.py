#!/usr/bin/env python3
"""Fail-closed Trust Center persistence and key-handling source contracts."""

from __future__ import annotations

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / (
    "hardware/nrf54l15clean/nrf54l15clean/libraries/"
    "Nrf54L15-Clean-Implementation/examples/Zigbee"
)
COORDINATORS = (
    EXAMPLES / "Coordinator/ZigbeeHaCoordinatorJoinDemo/ZigbeeHaCoordinatorJoinDemo.ino",
    EXAMPLES / "ZigbeeHaCoordinatorJoinDemo/ZigbeeHaCoordinatorJoinDemo.ino",
)


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
                return source[brace : index + 1]
    raise AssertionError(signature)


def validate_coordinator(path: pathlib.Path) -> None:
    source = path.read_text(encoding="utf-8")
    initialize = function_body(source, "bool initializeCoordinatorFrameCounters() {")
    persist = function_body(source, "bool persistCoordinatorSecurityState() {")
    allocate = function_body(
        source,
        "bool allocateCoordinatorFrameCounter(ZigbeeOutgoingCounterDomain domain,\n"
        "                                     uint32_t* outCounter) {",
    )
    incoming = function_body(source, "void processIncomingFrame(const ZigbeeFrame& frame) {")
    nwk_builder = function_body(source, "bool buildNwkCommandPsdu(")
    aps_builder = function_body(source, "                                     bool trackAck) {")
    transport_builder = function_body(
        source, "                           bool* outNwkSecurityEnabled) {"
    )
    rollout = function_body(source, "bool queueNetworkKeyUpdateRollout() {")
    factory_reset = function_body(source, "bool factoryResetCoordinatorSecurity() {")
    loop = function_body(source, "void loop() {")

    for token in (
        "loadWithStatus(&state)",
        "ZigbeePersistentLoadStatus::kEmpty",
        "ZigbeePersistentLoadStatus::kTombstone",
        "CracenRng rng",
        "rng.fill(g_activeNetworkKey",
        "keyIsAllZero(g_activeNetworkKey)",
        "state.peerSecurity",
        "peer.incomingNwkFrameCounter",
        "peer.networkKeySequence",
        "g_outgoingFrameCounters.restore(state)",
    ):
        assert token in initialize, (path, token)

    for token in (
        "state.peerSecurity[i]",
        "node.lastInboundSecurityFrameCounter",
        "node.currentNetworkKeySequence",
        "g_counterStore.save(state)",
        "g_outgoingFrameCountersReady = false",
    ):
        assert token in persist, (path, token)

    assert "g_outgoingFrameCountersReady = false" in allocate
    counter_update = incoming.index(
        "node->lastInboundSecurityFrameCounter = security.frameCounter"
    )
    durable = incoming.index("persistCoordinatorSecurityState()", counter_update)
    dispatch = incoming.index("nwk.frameType", durable)
    assert counter_update < durable < dispatch

    assert "securityExpected && networkKey == nullptr" in nwk_builder
    assert "uint8_t psdu[127]" not in nwk_builder
    assert "if (networkKey == nullptr)" in aps_builder
    assert "const bool useSecurity = true" in aps_builder
    assert "nwkSecurityExpected && nwkKey == nullptr" in transport_builder
    assert "nwk.securityEnabled = nwkSecurityExpected" in transport_builder

    assert "deriveAlternateNetworkKey" not in rollout
    assert "return false" in rollout
    assert "queue_key_update UNSUPPORTED" in source
    assert "0x5AU + nextSequence" not in source

    assert "g_counterStore.clear()" in factory_reset
    assert "initializeCoordinatorFrameCounters()" in factory_reset
    gate = loop.index("!g_outgoingFrameCountersReady || !g_radioReady")
    radio = loop.index("pumpRadio()")
    assert gate < radio
    print(f"PASS {path.relative_to(ROOT)}")


def main() -> None:
    for path in COORDINATORS:
        validate_coordinator(path)
    print("PASS all Zigbee Trust Center fail-closed security contracts")


if __name__ == "__main__":
    main()
