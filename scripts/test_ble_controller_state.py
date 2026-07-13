#!/usr/bin/env python3
"""Compile and execute deterministic BLE controller state regressions."""

from __future__ import annotations

import pathlib
import subprocess
import tempfile
import textwrap


ROOT = pathlib.Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src"


HARNESS = r"""
#include <cassert>
#include "ble_controller_state.h"

using namespace xiao_nrf54l15;

int main() {
  {
    const auto observation = bleLlObserveSequence(0x0d, true, 0, 1);
    assert(observation.peerAcknowledgedLastTx);
    assert(observation.packetIsNew);
    const auto update = bleLlApplyAuthenticatedSequence(observation, 1, 0, true);
    assert(update.expectedRxSn == 0);
    assert(update.nextTxSn == 1);
    assert(!update.txHistoryValid);
    assert(update.freshTxAllowed);
  }
  {
    // An LL control retransmission is acknowledged but never re-applied to
    // procedure state. The controller call site gates processing on this bit.
    const auto duplicate = bleLlObserveSequence(0x00, true, 0, 1);
    assert(!duplicate.packetIsNew);
  }
  {
    // A failed empty ACK has no payload or CCM transaction to retain.
    const auto commit = bleLlCommitTxAttempt(false, true, 0x01, 0, false);
    assert(!commit.retainHistory);
    assert(!commit.advanceEncryptionCounter);
    assert(commit.freshTxAllowed);
  }
  {
    // A timed-out non-empty data PDU may have reached the air. Preserve it
    // verbatim and reserve its CCM counter for retransmission.
    const auto commit = bleLlCommitTxAttempt(false, true, 0x02, 16, true);
    assert(commit.retainHistory);
    assert(commit.advanceEncryptionCounter);
    assert(!commit.freshTxAllowed);
  }
  {
    // Retransmission reuses ciphertext and never consumes another counter.
    const auto commit = bleLlCommitTxAttempt(true, false, 0x02, 16, true);
    assert(commit.retainHistory);
    assert(!commit.advanceEncryptionCounter);
  }
  {
    // A timed-out retransmission still owns the original history entry.
    const auto commit = bleLlCommitTxAttempt(false, false, 0x02, 16, true);
    assert(commit.retainHistory);
    assert(!commit.advanceEncryptionCounter);
    assert(!commit.freshTxAllowed);
  }
  {
    BleLlEncryptionFlags flags{};
    assert(bleLlEncryptionPhase(flags) == BleLlEncryptionPhase::kIdle);
    flags.sessionValid = true;
    flags.startRequestTxPending = true;
    assert(bleLlEncryptionPhase(flags) ==
           BleLlEncryptionPhase::kSendStartRequest);
    flags.startRequestPending = true;
    assert(bleLlEncryptionPhase(flags) == BleLlEncryptionPhase::kInvalid);
  }
  {
    BleLlEncryptionFlags flags{};
    flags.sessionValid = true;
    flags.rxEnabled = true;
    flags.txEnabled = true;
    assert(bleLlEncryptionPhase(flags) == BleLlEncryptionPhase::kEncrypted);
  }
  {
    bool startPending = true;
    bool startTxPending = false;
    bool awaiting = true;
    bool enableTx = true;
    assert(bleLlSetEncryptionProcedurePhase(
        BleLlEncryptionPhase::kSendStartRequest, &startPending,
        &startTxPending, &awaiting, &enableTx));
    assert(!startPending);
    assert(startTxPending);
    assert(!awaiting);
    assert(!enableTx);
  }
  {
    // Initial synchronization must stop at the CONNECT_IND transmit-window
    // boundary. Listening for most of the interval can overlap the next event
    // on the previous event's channel and prevent recovery.
    assert(bleLlInitialSyncWindowUs(1) == 1250U);
    assert(bleLlInitialSyncWindowUs(3) == 3750U);
    assert(bleLlInitialSyncWindowUs(8) == 10000U);
  }
  {
    // Android-style transcript: acknowledge a pairing response, miss an empty
    // ACK while DHKey work completes, then send and retry the queued response.
    uint8_t expectedRxSn = 0;
    uint8_t nextTxSn = 0;
    uint8_t lastTxSn = 0;
    bool historyValid = true;

    auto peer = bleLlObserveSequence(0x04, historyValid, lastTxSn,
                                     expectedRxSn);
    assert(peer.peerAcknowledgedLastTx && peer.packetIsNew);
    auto update = bleLlApplyAuthenticatedSequence(
        peer, expectedRxSn, nextTxSn, historyValid);
    expectedRxSn = update.expectedRxSn;
    nextTxSn = update.nextTxSn;
    historyValid = update.txHistoryValid;
    assert(expectedRxSn == 1 && nextTxSn == 1 && !historyValid);

    auto empty = bleLlCommitTxAttempt(false, true, 0x01, 0, false);
    historyValid = empty.retainHistory;
    assert(!historyValid && empty.freshTxAllowed);

    peer = bleLlObserveSequence(0x08, historyValid, lastTxSn, expectedRxSn);
    assert(peer.packetIsNew);
    update = bleLlApplyAuthenticatedSequence(peer, expectedRxSn, nextTxSn,
                                             historyValid);
    expectedRxSn = update.expectedRxSn;
    assert(expectedRxSn == 0);

    const auto dhKeyTx = bleLlCommitTxAttempt(true, true, 0x02, 21, false);
    assert(dhKeyTx.retainHistory && !dhKeyTx.freshTxAllowed);
    historyValid = true;
    lastTxSn = nextTxSn;

    const auto dhKeyRetry = bleLlCommitTxAttempt(false, false, 0x02, 21, false);
    assert(dhKeyRetry.retainHistory && !dhKeyRetry.freshTxAllowed);

    peer = bleLlObserveSequence(0x00, historyValid, lastTxSn, expectedRxSn);
    assert(peer.peerAcknowledgedLastTx && peer.packetIsNew);
    update = bleLlApplyAuthenticatedSequence(peer, expectedRxSn, nextTxSn,
                                             historyValid);
    assert(!update.txHistoryValid && update.freshTxAllowed);
  }
  return 0;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="ble-controller-state-") as tmp:
        tmp_path = pathlib.Path(tmp)
        source = tmp_path / "controller_state_test.cpp"
        binary = tmp_path / "controller_state_test"
        source.write_text(textwrap.dedent(HARNESS), encoding="ascii")
        subprocess.run(
            [
                "g++",
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(INCLUDE),
                str(source),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("PASS deterministic BLE sequence, TX transaction, and encryption-phase contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
