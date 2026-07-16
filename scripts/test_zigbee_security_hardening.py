#!/usr/bin/env python3
"""Native/model/source regressions for Zigbee authentication and TX counters."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src"
EXAMPLES = SRC.parent / "examples/Zigbee"
COUNTER_CPP = SRC / "zigbee_frame_counter.cpp"
COMMISSIONING = SRC / "zigbee_commissioning.cpp"
PERSISTENCE = SRC / "zigbee_persistence.cpp"


def require(text: str, tokens: tuple[str, ...], label: str) -> None:
    missing = [token for token in tokens if token not in text]
    assert not missing, f"{label} missing: {missing}"


def test_source_contracts() -> None:
    examples = list(EXAMPLES.rglob("*.ino"))
    joined = "\n".join(path.read_text() for path in examples)
    for forbidden in (
        "g_nwkSecurityFrameCounter++",
        "g_apsTrustCenterFrameCounter++",
        "nwkSecurityFrameCounter++",
        "trustedPlainSource",
    ):
        assert forbidden not in joined, f"obsolete security bypass remains: {forbidden}"

    protected = [
        path
        for path in examples
        if "ZigbeeOutgoingFrameCounterAllocator g_outgoingFrameCounters"
        in path.read_text()
    ]
    assert len(protected) == 17, f"expected 15 devices + 2 coordinators, got {len(protected)}"
    for path in protected:
        text = path.read_text()
        require(
            text,
            (
                "stampPersistentState(&state)",
                "ZigbeeOutgoingCounterDomain::kNwk",
            ),
            str(path.relative_to(ROOT)),
        )

    device_examples = [
        path for path in protected if "CoordinatorJoinDemo" not in str(path)
    ]
    assert len(device_examples) == 15
    for path in device_examples:
        text = path.read_text()
        label = str(path.relative_to(ROOT))
        require(
            text,
            (
                "commissioningCommandExpected",
                "waitingForJoinSecurityMaterial(g_network)",
                "(void)handleApsCommand(nwk.payload, nwk.payloadLength",
                "g_outgoingFrameCounters.restore(state)",
                "secureRejoinFrameCounter",
                "static bool g_persistentSecurityReady = false",
                "static bool g_radioReady = false",
                "g_store.loadWithStatus(&state)",
                "loadStatus == ZigbeePersistentLoadStatus::kLive",
                "loadStatus == ZigbeePersistentLoadStatus::kEmpty",
                "loadStatus == ZigbeePersistentLoadStatus::kTombstone",
                "state.ieeeAddress == kIeeeAddress",
                "!g_outgoingFrameCounters.enabled(ZigbeeOutgoingCounterDomain::kNwk)",
                "const bool saved = g_store.save(state)",
                "g_persistentSecurityReady = false",
                "ZigbeeOutgoingCounterDomain::kApsTrustCenter",
                "const bool persistenceOk = clearStore ? g_store.clear() : persistState()",
                "const bool storeOpen = g_store.begin(",
                "g_persistentSecurityReady && g_radio.begin(g_channel, 8)",
                "if (!g_persistentSecurityReady || !g_radioReady)",
                "persistence blocked; use c to clear or s for status",
            ),
            label,
        )
        assert "g_store.load(" not in text, f"non-strict state load remains in {label}"
        for forbidden_debug in ("Preferences debugPrefs", 'begin("zbdebug"',
                                "verify_short="):
            assert forbidden_debug not in text, (
                f"temporary persistence diagnostic remains in {label}: "
                f"{forbidden_debug}"
            )

        restore_at = text.index("void restoreState()")
        clear_at = text.index("bool clearJoinState(bool clearStore)", restore_at)
        restore = text[restore_at:clear_at]
        assert restore.index("loadWithStatus(&state)") < restore.index(
            "loadStatus == ZigbeePersistentLoadStatus::kLive"
        )
        assert restore.index("g_outgoingFrameCounters.restore(state)") < restore.index(
            "!g_outgoingFrameCounters.enabled(ZigbeeOutgoingCounterDomain::kNwk)"
        )
        assert "if (g_persistentSecurityReady && !g_joined && !g_rejoinPending)" in restore or (
            "ZigbeeSleepyOnOffButton" in label
        )

        process_at = text.index("void processIncomingFrame(")
        process = text[process_at:text.index("void pollCoordinator()", process_at)]
        replay_update = process.index(
            "g_lastInboundSecurityFrameCounter = security.frameCounter"
        )
        durable_update = process.index("if (!persistState())", replay_update)
        plaintext_branch = process.index("if (!security.valid)", durable_update)
        assert replay_update < durable_update < plaintext_branch, (
            f"NWK replay counter is not durable before dispatch in {label}"
        )

        aps_at = text.index("bool handleApsCommand(")
        aps_end = text.index("bool activeScan(", aps_at)
        aps = text[aps_at:aps_end]
        transport_apply = aps.index("applyTransportKeyInstall")
        transport_save = aps.index("if (!persistState())", transport_apply)
        transport_log = aps.index('Serial.print("transport_key', transport_save)
        update_apply = aps.index("applyUpdateDevice")
        update_save = aps.index("if (!persistState())", update_apply)
        update_log = aps.index('Serial.print("update_device', update_save)
        switch_apply = aps.index("applySwitchKey")
        switch_save = aps.index("if (!persistState())", switch_apply)
        switch_log = aps.index('Serial.print("switch_key', switch_save)
        assert transport_apply < transport_save < transport_log
        assert update_apply < update_save < update_log
        assert switch_apply < switch_save < switch_log

    coordinators = [
        EXAMPLES / "Coordinator/ZigbeeHaCoordinatorJoinDemo/ZigbeeHaCoordinatorJoinDemo.ino",
        EXAMPLES / "ZigbeeHaCoordinatorJoinDemo/ZigbeeHaCoordinatorJoinDemo.ino",
    ]
    for path in coordinators:
        text = path.read_text()
        require(
            text,
            (
                'g_counterStore.begin("zbcoordctr")',
                "loadWithStatus(&state)",
                "status == ZigbeePersistentLoadStatus::kEmpty",
                "status != ZigbeePersistentLoadStatus::kLive",
                "ZigbeeOutgoingCounterDomain::kApsTrustCenter",
                "g_outgoingFrameCountersReady && g_radio.begin",
                "securityExpected && networkKey == nullptr",
            ),
            str(path.relative_to(ROOT)),
        )
        decrypt_at = text.index("ZigbeeSecurity::parseSecuredNwkFrame(", text.index("void processIncomingFrame"))
        command_at = text.index("if (nwk.frameType == ZigbeeNwkFrameType::kCommand)", decrypt_at)
        inbound_path = text[decrypt_at:command_at]
        assert "ZigbeeCodec::parseNwkFrame" not in inbound_path, (
            f"coordinator plaintext fallback remains in {path}"
        )

    commissioning = COMMISSIONING.read_text()
    require(
        commissioning,
        (
            "uint32_t frameCounter)",
            "security.frameCounter = frameCounter",
            "frameCounter != UINT32_MAX",
        ),
        "commissioning reserved counter handoff",
    )
    assert "state->nwkSecurityFrameCounter++" not in commissioning

    persistence = PERSISTENCE.read_text()
    require(
        persistence,
        (
            "ZigbeePersistentLoadStatus::kEmpty",
            "ZigbeePersistentLoadStatus::kCorrupt",
            "ZigbeePersistentLoadStatus::kNamespaceCollision",
            "journalSlotIsErased(firstSlot)",
            "Legacy state is only an admissible migration source",
        ),
        "strict persistence load status",
    )
    load_body = persistence[
        persistence.index("bool ZigbeePersistentStateStore::load(") :
        persistence.index("ZigbeePersistentLoadStatus ZigbeePersistentStateStore::loadWithStatus(")
    ]
    assert load_body.index("slotAForeign || slotBForeign") < load_body.index(
        "newestValidSlotForNamespace"
    )
    assert load_body.index("journalSlotIsErased(firstSlot)") < load_body.index(
        "prefs_.getBytesLength"
    )
    print("PASS Zigbee plaintext rejection and persisted-counter source contracts")


def inspect_model(slot_a: str, slot_b: str) -> str:
    slots = (slot_a, slot_b)
    if "foreign" in slots:
        return "collision"
    if "torn" in slots:
        # A torn-looking sibling may instead be a newer committed frame-counter
        # reservation that suffered latent corruption. Security loads cannot
        # safely fall back to the older valid generation.
        return "corrupt"
    matching = [slot for slot in slots if slot in ("live", "tombstone")]
    if matching:
        return matching[-1]
    if all(slot in ("zero", "ones") for slot in slots):
        return "empty"
    return "corrupt"


def test_load_status_model() -> None:
    assert inspect_model("zero", "ones") == "empty"
    assert inspect_model("torn", "zero") == "corrupt"
    assert inspect_model("foreign", "zero") == "collision"
    assert inspect_model("live", "foreign") == "collision"
    assert inspect_model("live", "torn") == "corrupt"
    assert inspect_model("live", "tombstone") == "tombstone"
    print("PASS erased/live/tombstone/corrupt/collision load-status model")


PREFERENCES_STUB = r"""
#pragma once
class Preferences {
 public:
  Preferences() = default;
};
"""


NATIVE_HARNESS = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "zigbee_frame_counter.h"

using namespace xiao_nrf54l15;

namespace {
ZigbeePersistentState g_fakeState{};
ZigbeePersistentLoadStatus g_fakeStatus = ZigbeePersistentLoadStatus::kLive;
bool g_saveOk = true;
uint32_t g_saveCalls = 0U;
}

namespace xiao_nrf54l15 {
ZigbeePersistentStateStore::ZigbeePersistentStateStore()
    : prefs_(), open_(true), legacyPrefsOpen_(false), namespaceLength_(0U),
      namespaceName_{0}, namespaceHash_(1U) {}

ZigbeePersistentLoadStatus ZigbeePersistentStateStore::loadWithStatus(
    ZigbeePersistentState* outState) {
  if (outState == nullptr) return ZigbeePersistentLoadStatus::kNotOpen;
  *outState = g_fakeState;
  return g_fakeStatus;
}

bool ZigbeePersistentStateStore::save(const ZigbeePersistentState& state) {
  ++g_saveCalls;
  if (!g_saveOk) return false;
  g_fakeState = state;
  return true;
}

bool ZigbeePersistentStateStore::reserveOutgoingFrameCounterRange(
    uint8_t persistentFlag, bool apsTrustCenter,
    uint32_t expectedHighWater, uint32_t reservedHighWater) {
  ++g_saveCalls;
  if (!g_saveOk || (g_fakeState.flags & persistentFlag) == 0U) return false;
  uint32_t& stored = apsTrustCenter ? g_fakeState.apsFrameCounter
                                    : g_fakeState.nwkFrameCounter;
  if (stored != expectedHighWater || reservedHighWater <= stored) return false;
  stored = reservedHighWater;
  g_fakeState.flags = static_cast<uint8_t>(g_fakeState.flags | persistentFlag);
  return true;
}
}

static void seed(ZigbeeOutgoingFrameCounterAllocator* allocator,
                 ZigbeePersistentStateStore* store) {
  g_fakeState = ZigbeePersistentState{};
  allocator->stampPersistentState(&g_fakeState);
  g_fakeStatus = ZigbeePersistentLoadStatus::kLive;
  g_saveOk = true;
  g_saveCalls = 0U;
  (void)store;
}

int main() {
  ZigbeePersistentStateStore store;
  ZigbeeOutgoingFrameCounterAllocator allocator;
  allocator.resetForNewKey(ZigbeeOutgoingCounterDomain::kNwk);
  seed(&allocator, &store);

  uint32_t counter = 99U;
  assert(allocator.allocate(ZigbeeOutgoingCounterDomain::kNwk, &store,
                            &counter));
  assert(counter == 1U);
  assert(g_saveCalls == 1U);
  assert(g_fakeState.nwkFrameCounter == 65U);
  assert((g_fakeState.flags & kZigbeePersistentFlagNwkCounterHighWater) != 0U);
  for (uint32_t expected = 2U; expected <= 64U; ++expected) {
    assert(allocator.allocate(ZigbeeOutgoingCounterDomain::kNwk, &store,
                              &counter));
    assert(counter == expected);
  }
  assert(g_saveCalls == 1U);

  ZigbeePersistentState appSave = g_fakeState;
  appSave.levelState = 77U;
  allocator.stampPersistentState(&appSave);
  assert(appSave.nwkFrameCounter == 65U);

  // A reset abandons the old block and reserves the next block before use.
  g_fakeState = appSave;
  ZigbeeOutgoingFrameCounterAllocator rebooted;
  rebooted.restore(g_fakeState);
  assert(rebooted.allocate(ZigbeeOutgoingCounterDomain::kNwk, &store,
                           &counter));
  assert(counter == 65U);
  assert(g_fakeState.nwkFrameCounter == 129U);

  // A failed high-water commit never releases a counter.
  ZigbeeOutgoingFrameCounterAllocator failed;
  failed.resetForNewKey(ZigbeeOutgoingCounterDomain::kNwk);
  seed(&failed, &store);
  g_saveOk = false;
  counter = 99U;
  assert(!failed.allocate(ZigbeeOutgoingCounterDomain::kNwk, &store,
                          &counter));
  assert(counter == 0U);
  assert(failed.nextCounter(ZigbeeOutgoingCounterDomain::kNwk) == 1U);
  assert(failed.highWater(ZigbeeOutgoingCounterDomain::kNwk) == 1U);

  // Legacy next-counter fields have no proof of reservation and fail closed.
  ZigbeePersistentState legacy{};
  legacy.nwkFrameCounter = 1234U;
  ZigbeeOutgoingFrameCounterAllocator legacyAllocator;
  legacyAllocator.restore(legacy);
  g_fakeState = legacy;
  g_saveOk = true;
  assert(!legacyAllocator.allocate(ZigbeeOutgoingCounterDomain::kNwk, &store,
                                   &counter));

  // NWK and APS trust-center domains reserve independently.
  ZigbeeOutgoingFrameCounterAllocator dual;
  dual.resetForNewKey(ZigbeeOutgoingCounterDomain::kNwk);
  dual.resetForNewKey(ZigbeeOutgoingCounterDomain::kApsTrustCenter);
  seed(&dual, &store);
  assert(dual.allocate(ZigbeeOutgoingCounterDomain::kApsTrustCenter, &store,
                       &counter));
  assert(counter == 1U);
  assert(g_fakeState.apsFrameCounter == 65U);
  assert(g_fakeState.nwkFrameCounter == 1U);
  assert(dual.allocate(ZigbeeOutgoingCounterDomain::kNwk, &store, &counter));
  assert(counter == 1U);
  assert(g_fakeState.nwkFrameCounter == 65U);
  assert(g_fakeState.apsFrameCounter == 65U);

  // UINT32_MAX is representable as an exclusive end, but never issued.
  ZigbeePersistentState nearLimit{};
  nearLimit.flags = kZigbeePersistentFlagNwkCounterHighWater;
  nearLimit.nwkFrameCounter = UINT32_MAX - 64U;
  ZigbeeOutgoingFrameCounterAllocator nearLimitAllocator;
  nearLimitAllocator.restore(nearLimit);
  g_fakeState = nearLimit;
  for (uint32_t i = 0U; i < 64U; ++i) {
    assert(nearLimitAllocator.allocate(ZigbeeOutgoingCounterDomain::kNwk,
                                       &store, &counter));
    assert(counter == (UINT32_MAX - 64U) + i);
  }
  assert(counter == UINT32_MAX - 1U);
  assert(g_fakeState.nwkFrameCounter == UINT32_MAX);
  assert(!nearLimitAllocator.allocate(ZigbeeOutgoingCounterDomain::kNwk,
                                      &store, &counter));
  assert(counter == 0U);
  return 0;
}
"""


def test_native_allocator() -> None:
    with tempfile.TemporaryDirectory(prefix="zigbee-counter-test-") as tmp:
        tmpdir = Path(tmp)
        (tmpdir / "Preferences.h").write_text(PREFERENCES_STUB)
        harness = tmpdir / "counter_harness.cpp"
        harness.write_text(NATIVE_HARNESS)
        binary = tmpdir / "counter_harness"
        subprocess.run(
            [
                "g++",
                "-std=gnu++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DNRF54L15_CLEAN_ZIGBEE_ENABLED=1",
                "-I",
                str(tmpdir),
                "-I",
                str(SRC),
                str(COUNTER_CPP),
                str(harness),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("PASS native crash-safe NWK/APS outgoing counter allocator")


def main() -> None:
    test_source_contracts()
    test_load_status_model()
    test_native_allocator()
    print("PASS all Zigbee security hardening regressions")


if __name__ == "__main__":
    main()
