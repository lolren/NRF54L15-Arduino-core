#ifndef NRF54L15_CLEAN_BLE_CONTROLLER_STATE_H
#define NRF54L15_CLEAN_BLE_CONTROLLER_STATE_H

#include <stdint.h>

namespace xiao_nrf54l15 {

enum class BleLlEncryptionPhase : uint8_t {
  kIdle = 0,
  kKeyDerivation,
  kPeripheralAwaitStartRequest,
  kCompatibilitySendStartRequest,
  kAwaitStartResponse,
  kEnableTxOnNextEvent,
  kEncrypted,
  kInvalid,
};

struct BleLlEncryptionFlags {
  bool sessionValid;
  bool rxEnabled;
  bool txEnabled;
  bool keyDerivationPending;
  bool startRequestPending;
  bool startRequestTxPending;
  bool awaitingStartResponse;
  bool enableTxOnNextEvent;
};

inline BleLlEncryptionPhase bleLlEncryptionPhase(
    const BleLlEncryptionFlags& flags) {
  if (flags.keyDerivationPending) {
    return flags.sessionValid ? BleLlEncryptionPhase::kKeyDerivation
                              : BleLlEncryptionPhase::kInvalid;
  }
  if (!flags.sessionValid) {
    return (!flags.rxEnabled && !flags.txEnabled &&
            !flags.startRequestPending && !flags.startRequestTxPending &&
            !flags.awaitingStartResponse && !flags.enableTxOnNextEvent)
               ? BleLlEncryptionPhase::kIdle
               : BleLlEncryptionPhase::kInvalid;
  }
  const uint8_t procedureCount =
      static_cast<uint8_t>(flags.startRequestPending) +
      static_cast<uint8_t>(flags.startRequestTxPending) +
      static_cast<uint8_t>(flags.awaitingStartResponse) +
      static_cast<uint8_t>(flags.enableTxOnNextEvent);
  if (procedureCount > 1U) {
    return BleLlEncryptionPhase::kInvalid;
  }
  if (flags.startRequestPending) {
    return (!flags.txEnabled)
               ? BleLlEncryptionPhase::kPeripheralAwaitStartRequest
               : BleLlEncryptionPhase::kInvalid;
  }
  if (flags.startRequestTxPending) {
    return (!flags.rxEnabled && !flags.txEnabled)
               ? BleLlEncryptionPhase::kCompatibilitySendStartRequest
               : BleLlEncryptionPhase::kInvalid;
  }
  if (flags.awaitingStartResponse) {
    return BleLlEncryptionPhase::kAwaitStartResponse;
  }
  if (flags.enableTxOnNextEvent) {
    return (flags.rxEnabled && !flags.txEnabled)
               ? BleLlEncryptionPhase::kEnableTxOnNextEvent
               : BleLlEncryptionPhase::kInvalid;
  }
  return (flags.rxEnabled && flags.txEnabled)
             ? BleLlEncryptionPhase::kEncrypted
             : BleLlEncryptionPhase::kInvalid;
}

inline bool bleLlSetEncryptionProcedurePhase(
    BleLlEncryptionPhase phase, bool* startRequestPending,
    bool* startRequestTxPending, bool* awaitingStartResponse,
    bool* enableTxOnNextEvent) {
  if (startRequestPending == nullptr || startRequestTxPending == nullptr ||
      awaitingStartResponse == nullptr || enableTxOnNextEvent == nullptr) {
    return false;
  }
  *startRequestPending = false;
  *startRequestTxPending = false;
  *awaitingStartResponse = false;
  *enableTxOnNextEvent = false;
  switch (phase) {
    case BleLlEncryptionPhase::kIdle:
    case BleLlEncryptionPhase::kEncrypted:
      return true;
    case BleLlEncryptionPhase::kPeripheralAwaitStartRequest:
      *startRequestPending = true;
      return true;
    case BleLlEncryptionPhase::kCompatibilitySendStartRequest:
      *startRequestTxPending = true;
      return true;
    case BleLlEncryptionPhase::kAwaitStartResponse:
      *awaitingStartResponse = true;
      return true;
    case BleLlEncryptionPhase::kEnableTxOnNextEvent:
      *enableTxOnNextEvent = true;
      return true;
    case BleLlEncryptionPhase::kKeyDerivation:
    case BleLlEncryptionPhase::kInvalid:
      return false;
  }
  return false;
}

struct BleLlSequenceObservation {
  bool peerAcknowledgedLastTx;
  bool packetIsNew;
  uint8_t peerNesn;
  uint8_t peerSn;
};

inline BleLlSequenceObservation bleLlObserveSequence(
    uint8_t header, bool txHistoryValid, uint8_t lastTxSn,
    uint8_t expectedRxSn) {
  const uint8_t peerNesn = static_cast<uint8_t>((header >> 2U) & 0x01U);
  const uint8_t peerSn = static_cast<uint8_t>((header >> 3U) & 0x01U);
  return {
      txHistoryValid && (peerNesn != (lastTxSn & 0x01U)),
      peerSn == (expectedRxSn & 0x01U),
      peerNesn,
      peerSn,
  };
}

struct BleLlAuthenticatedSequenceUpdate {
  uint8_t expectedRxSn;
  uint8_t nextTxSn;
  bool txHistoryValid;
  bool freshTxAllowed;
};

inline BleLlAuthenticatedSequenceUpdate bleLlApplyAuthenticatedSequence(
    const BleLlSequenceObservation& observation, uint8_t expectedRxSn,
    uint8_t nextTxSn, bool txHistoryValid) {
  return {
      static_cast<uint8_t>(expectedRxSn ^
                           static_cast<uint8_t>(observation.packetIsNew)),
      static_cast<uint8_t>(nextTxSn ^
                           static_cast<uint8_t>(observation.peerAcknowledgedLastTx)),
      observation.peerAcknowledgedLastTx ? false : txHistoryValid,
      observation.peerAcknowledgedLastTx || !txHistoryValid,
  };
}

struct BleLlTxAttemptCommit {
  bool retainHistory;
  bool advanceEncryptionCounter;
  bool freshTxAllowed;
};

constexpr bool bleLlIsDataPdu(uint8_t llid) {
  return llid == 0x01U || llid == 0x02U;
}

inline BleLlTxAttemptCommit bleLlCommitTxAttempt(
    bool txCompleted, bool freshPayload, uint8_t llid, uint8_t plainLength,
    bool encrypted) {
  if (!freshPayload) {
    // A retry owns an existing history entry. A timeout cannot retire it.
    return {true, false, false};
  }
  const bool uncertainNonEmptyData =
      !txCompleted && plainLength > 0U && bleLlIsDataPdu(llid);
  const bool retain = txCompleted || uncertainNonEmptyData;
  return {
      retain,
      encrypted && retain,
      !retain || plainLength == 0U,
  };
}

}  // namespace xiao_nrf54l15

#endif
