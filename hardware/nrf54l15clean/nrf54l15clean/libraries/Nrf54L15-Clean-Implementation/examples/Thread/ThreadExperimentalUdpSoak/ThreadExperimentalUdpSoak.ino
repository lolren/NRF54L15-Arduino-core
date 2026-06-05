// ThreadExperimentalUdpSoak
//
// Two-board staged Thread UDP reliability probe. Flash the same sketch to two
// boards with Tools > Thread Core > Experimental Stage Core. One board should
// become leader. The child/router sends payloads to the leader, the leader
// sends the same payload matrix back, then the child/router probes multicast
// delivery. The sketch also exports the active dataset hex so reboot/rejoin
// behavior can be checked from the serial log.

#include <nrf54_all.h>

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core before building."
#endif

#include <string.h>

using xiao_nrf54l15::Nrf54ThreadExperimental;

extern "C" {
__attribute__((used)) volatile uint32_t g_soak_results[20] = {0};
}

namespace {

#define SOAK_PRINT(value) do { if (Serial) Serial.print(value); if (Serial1) Serial1.print(value); } while (0)
#define SOAK_PRINTLN(...) do { if (Serial) Serial.println(__VA_ARGS__); if (Serial1) Serial1.println(__VA_ARGS__); } while (0)
#define SOAK_PRINT_HEX(value) do { if (Serial) Serial.print(value, HEX); if (Serial1) Serial1.print(value, HEX); } while (0)

constexpr uint16_t kUdpPort = 61631U;
constexpr uint32_t kSendIntervalMs = 500UL;
constexpr uint32_t kAckTimeoutMs = 4000UL;
constexpr uint32_t kStatusIntervalMs = 2000UL;
constexpr uint8_t kMaxRetriesPerPayload = 3U;
constexpr uint16_t kMaxPayloadLength = 512U;
constexpr uint8_t kMagic = 0x54U;
constexpr uint8_t kPingType = 0x50U;
constexpr uint8_t kAckType = 0x41U;

constexpr uint16_t kPayloadSizes[] = {
    8U, 16U, 31U, 63U, 95U, 127U, 191U, 255U, 512U,
};
constexpr size_t kPayloadSizeCount =
    sizeof(kPayloadSizes) / sizeof(kPayloadSizes[0]);

constexpr uint8_t kMulticastAddrBytes[16] = {
    0xFF, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
};

Nrf54ThreadExperimental gThread;
Nrf54ThreadExperimental::Role gLastRole =
    Nrf54ThreadExperimental::Role::kUnknown;

enum class SoakTxPhase : uint8_t {
  kUplink = 0U,
  kDownlink = 1U,
  kMulticast = 2U,
};

uint8_t gTxBuffer[kMaxPayloadLength] = {0};
uint16_t gCurrentSeq = 1U;
size_t gUplinkIndex = 0U;
size_t gDownlinkIndex = 0U;
size_t gDownlinkRxIndex = 0U;
size_t gObservedUplinkIndex = 0U;
size_t gMulticastIndex = 0U;
uint32_t gLastSendMs = 0U;
uint32_t gLastPrintMs = 0U;
uint8_t gRetryCount = 0U;
SoakTxPhase gCurrentTxPhase = SoakTxPhase::kUplink;
bool gWaitingForAck = false;
bool gTestStarted = false;
bool gUplinkDone = false;
bool gObservedUplinkDone = false;
bool gDownlinkStarted = false;
bool gDownlinkDone = false;
bool gDownlinkReceiverDone = false;
bool gMulticastStarted = false;
bool gMulticastDone = false;
bool gMulticastSubscribed = false;
bool gDatasetExported = false;
bool gPeerKnown = false;
otIp6Address gPeerAddr = {};
uint16_t gPeerPort = kUdpPort;

uint32_t gPingTxCount = 0U;
uint32_t gPingRxCount = 0U;
uint32_t gDownlinkRxCount = 0U;
uint32_t gAckTxCount = 0U;
uint32_t gAckRxCount = 0U;
uint32_t gUplinkPassCount = 0U;
uint32_t gUplinkFailCount = 0U;
uint32_t gDownlinkPassCount = 0U;
uint32_t gDownlinkFailCount = 0U;
uint32_t gMulticastPassCount = 0U;
uint32_t gMulticastFailCount = 0U;
uint32_t gRetryTotal = 0U;
uint32_t gInvalidRxCount = 0U;
uint16_t gLastLength = 0U;
uint16_t gLastAckSeq = 0U;
uint32_t gLastFinalMatrixMs = 0U;
uint32_t gLastPartitionId = 0U;
bool gLastPartitionValid = false;
uint8_t gUplinkResults[kPayloadSizeCount] = {0};
uint8_t gDownlinkResults[kPayloadSizeCount] = {0};
uint8_t gMulticastResults[kPayloadSizeCount] = {0};

char gDatasetHex[(OT_OPERATIONAL_DATASET_MAX_LENGTH * 2U) + 1U] = {0};

uint8_t checksum8(const uint8_t* data, uint16_t length) {
  uint8_t checksum = 0U;
  if (data == nullptr) return checksum;
  for (uint16_t i = 0U; i < length; ++i) {
    checksum = static_cast<uint8_t>((checksum << 1U) | (checksum >> 7U));
    checksum ^= data[i];
    checksum = static_cast<uint8_t>(checksum + 0x3DU);
  }
  return checksum;
}

bool buildPayload(uint8_t type, uint16_t seq, uint16_t length) {
  if (length < 8U || length > sizeof(gTxBuffer)) return false;
  memset(gTxBuffer, 0, sizeof(gTxBuffer));
  gTxBuffer[0] = kMagic;
  gTxBuffer[1] = type;
  gTxBuffer[2] = static_cast<uint8_t>(seq & 0xFFU);
  gTxBuffer[3] = static_cast<uint8_t>((seq >> 8U) & 0xFFU);
  gTxBuffer[4] = static_cast<uint8_t>(length & 0xFFU);
  gTxBuffer[5] = static_cast<uint8_t>((length >> 8U) & 0xFFU);
  for (uint16_t i = 6U; i < (length - 1U); ++i) {
    gTxBuffer[i] = static_cast<uint8_t>((seq * 33U) + (i * 17U) + length);
  }
  gTxBuffer[length - 1U] = checksum8(gTxBuffer, length - 1U);
  return true;
}

bool parsePayload(const uint8_t* payload, uint16_t length, uint8_t* outType,
                  uint16_t* outSeq, uint16_t* outDeclaredLength) {
  if (payload == nullptr || outType == nullptr || outSeq == nullptr ||
      outDeclaredLength == nullptr || length < 8U) {
    return false;
  }
  if (payload[0] != kMagic) return false;
  const uint16_t declaredLength =
      static_cast<uint16_t>(payload[4]) |
      (static_cast<uint16_t>(payload[5]) << 8U);
  if (declaredLength != length) return false;
  if (checksum8(payload, length - 1U) != payload[length - 1U]) return false;
  *outType = payload[1];
  *outSeq = static_cast<uint16_t>(payload[2]) |
            (static_cast<uint16_t>(payload[3]) << 8U);
  *outDeclaredLength = declaredLength;
  return true;
}

const char* failModeName(uint8_t result) {
  switch (result) {
    case 2: return "tx";
    case 4: return "timeout";
    case 5: return "checksum";
    default: return "unknown";
  }
}

const char* resultName(uint8_t result) {
  return result == 1U ? "pass" : failModeName(result);
}

const char* phaseName(SoakTxPhase phase) {
  switch (phase) {
    case SoakTxPhase::kUplink: return "uplink";
    case SoakTxPhase::kDownlink: return "downlink";
    case SoakTxPhase::kMulticast: return "multicast";
    default: return "unknown";
  }
}

void updateResults() {
  g_soak_results[0] = gThread.started() ? 1U : 0U;
  g_soak_results[1] = gThread.attached() ? 1U : 0U;
  g_soak_results[2] = static_cast<uint32_t>(gThread.role());
  g_soak_results[3] = gThread.rloc16();
  g_soak_results[4] = gUplinkPassCount;
  g_soak_results[5] = gUplinkFailCount;
  g_soak_results[6] = gDownlinkPassCount;
  g_soak_results[7] = gDownlinkFailCount;
  g_soak_results[8] = gMulticastPassCount;
  g_soak_results[9] = gMulticastFailCount;
  g_soak_results[10] = gUplinkDone ? 1U : 0U;
  g_soak_results[11] = gDownlinkDone ? 1U : 0U;
  g_soak_results[12] = gDownlinkReceiverDone ? 1U : 0U;
  g_soak_results[13] = gMulticastDone ? 1U : 0U;
  g_soak_results[14] = gDatasetExported ? 1U : 0U;
  g_soak_results[15] = gMulticastSubscribed ? 1U : 0U;
  g_soak_results[16] = gPeerKnown ? 1U : 0U;
}

void recordResult(SoakTxPhase phase, size_t idx, uint8_t result) {
  if (idx >= kPayloadSizeCount) return;
  const uint16_t len = kPayloadSizes[idx];
  const bool pass = (result == 1U);
  const char* prefix = "soak_fail len=";

  switch (phase) {
    case SoakTxPhase::kUplink:
      gUplinkResults[idx] = result;
      if (pass) {
        ++gUplinkPassCount;
        prefix = "soak_pass len=";
      } else {
        ++gUplinkFailCount;
      }
      break;
    case SoakTxPhase::kDownlink:
      gDownlinkResults[idx] = result;
      if (pass) {
        ++gDownlinkPassCount;
        prefix = "soak_downlink_pass len=";
      } else {
        ++gDownlinkFailCount;
        prefix = "soak_downlink_fail len=";
      }
      break;
    case SoakTxPhase::kMulticast:
      gMulticastResults[idx] = result;
      if (pass) {
        ++gMulticastPassCount;
        prefix = "soak_mcast_pass len=";
      } else {
        ++gMulticastFailCount;
        prefix = "soak_mcast_fail len=";
      }
      break;
  }

  SOAK_PRINT(prefix);
  SOAK_PRINT(len);
  if (!pass) {
    SOAK_PRINT(" mode=");
    SOAK_PRINT(failModeName(result));
  }
  SOAK_PRINTLN("");
}

void printResultMatrix() {
  for (size_t i = 0U; i < kPayloadSizeCount; ++i) {
    SOAK_PRINT("soak_result len=");
    SOAK_PRINT(kPayloadSizes[i]);
    SOAK_PRINT(" uplink=");
    SOAK_PRINT(resultName(gUplinkResults[i]));
    SOAK_PRINT(" downlink=");
    SOAK_PRINT(resultName(gDownlinkResults[i]));
    SOAK_PRINT(" multicast=");
    SOAK_PRINTLN(resultName(gMulticastResults[i]));
  }
}

void printStatus(const char* reason) {
  SOAK_PRINT("soak_stat reason=");
  SOAK_PRINT(reason);
  SOAK_PRINT(" role=");
  SOAK_PRINT(gThread.roleName());
  SOAK_PRINT(" rloc16=0x");
  SOAK_PRINT_HEX(gThread.rloc16());
  SOAK_PRINT(" part=0x");
  SOAK_PRINT_HEX(gThread.partitionId());
  SOAK_PRINT(" seq=");
  SOAK_PRINT(gCurrentSeq);
  SOAK_PRINT(" len=");
  SOAK_PRINT(gLastLength);
  SOAK_PRINT(" ping=");
  SOAK_PRINT(gPingTxCount);
  SOAK_PRINT("/");
  SOAK_PRINT(gPingRxCount);
  SOAK_PRINT(" ack=");
  SOAK_PRINT(gAckTxCount);
  SOAK_PRINT("/");
  SOAK_PRINT(gAckRxCount);
  SOAK_PRINT(" phase=");
  SOAK_PRINT(phaseName(gCurrentTxPhase));
  SOAK_PRINT(" uplink=");
  SOAK_PRINT(gUplinkPassCount);
  SOAK_PRINT("/");
  SOAK_PRINT(gUplinkFailCount);
  SOAK_PRINT(" downlink=");
  SOAK_PRINT(gDownlinkPassCount);
  SOAK_PRINT("/");
  SOAK_PRINT(gDownlinkFailCount);
  SOAK_PRINT(" downlink_rx=");
  SOAK_PRINT(gDownlinkRxCount);
  SOAK_PRINT(" mcast=");
  SOAK_PRINT(gMulticastPassCount);
  SOAK_PRINT("/");
  SOAK_PRINT(gMulticastFailCount);
  SOAK_PRINT(" retry=");
  SOAK_PRINT(gRetryTotal);
  SOAK_PRINT(" invalid=");
  SOAK_PRINT(gInvalidRxCount);
  SOAK_PRINT(" wait=");
  SOAK_PRINT(gWaitingForAck ? 1 : 0);
  SOAK_PRINT(" uplink_done=");
  SOAK_PRINT(gUplinkDone ? 1 : 0);
  SOAK_PRINT(" observed_uplink_done=");
  SOAK_PRINT(gObservedUplinkDone ? 1 : 0);
  SOAK_PRINT(" downlink_done=");
  SOAK_PRINT(gDownlinkDone ? 1 : 0);
  SOAK_PRINT(" downlink_rx_done=");
  SOAK_PRINT(gDownlinkReceiverDone ? 1 : 0);
  SOAK_PRINT(" mcast_done=");
  SOAK_PRINT(gMulticastDone ? 1 : 0);
  SOAK_PRINT(" mcast_sub=");
  SOAK_PRINT(gMulticastSubscribed ? 1 : 0);
  SOAK_PRINT(" peer=");
  SOAK_PRINT(gPeerKnown ? 1 : 0);
  SOAK_PRINT(" err=");
  SOAK_PRINT(static_cast<int>(gThread.lastError()));
  SOAK_PRINT("/");
  SOAK_PRINTLN(static_cast<int>(gThread.lastUdpError()));
}

void printDone() {
  SOAK_PRINT("soak_done uplink_pass=");
  SOAK_PRINT(gUplinkPassCount);
  SOAK_PRINT(" uplink_fail=");
  SOAK_PRINT(gUplinkFailCount);
  SOAK_PRINT(" downlink_pass=");
  SOAK_PRINT(gDownlinkPassCount);
  SOAK_PRINT(" downlink_fail=");
  SOAK_PRINT(gDownlinkFailCount);
  SOAK_PRINT(" mcast_pass=");
  SOAK_PRINT(gMulticastPassCount);
  SOAK_PRINT(" mcast_fail=");
  SOAK_PRINTLN(gMulticastFailCount);
  printResultMatrix();
}

void resetSoakProgress(const char* reason) {
  gCurrentSeq = 1U;
  gUplinkIndex = 0U;
  gDownlinkIndex = 0U;
  gDownlinkRxIndex = 0U;
  gObservedUplinkIndex = 0U;
  gMulticastIndex = 0U;
  gRetryCount = 0U;
  gCurrentTxPhase = SoakTxPhase::kUplink;
  gWaitingForAck = false;
  gTestStarted = false;
  gUplinkDone = false;
  gObservedUplinkDone = false;
  gDownlinkStarted = false;
  gDownlinkDone = false;
  gDownlinkReceiverDone = false;
  gMulticastStarted = false;
  gMulticastDone = false;
  gPeerKnown = false;
  gPeerPort = kUdpPort;

  gPingTxCount = 0U;
  gPingRxCount = 0U;
  gDownlinkRxCount = 0U;
  gAckTxCount = 0U;
  gAckRxCount = 0U;
  gUplinkPassCount = 0U;
  gUplinkFailCount = 0U;
  gDownlinkPassCount = 0U;
  gDownlinkFailCount = 0U;
  gMulticastPassCount = 0U;
  gMulticastFailCount = 0U;
  gRetryTotal = 0U;
  gInvalidRxCount = 0U;
  gLastLength = 0U;
  gLastAckSeq = 0U;
  gLastSendMs = millis();
  gLastFinalMatrixMs = 0U;
  memset(gUplinkResults, 0, sizeof(gUplinkResults));
  memset(gDownlinkResults, 0, sizeof(gDownlinkResults));
  memset(gMulticastResults, 0, sizeof(gMulticastResults));

  SOAK_PRINT("soak_reset reason=");
  SOAK_PRINTLN(reason);
}

void trackPartitionAndResetIfNeeded() {
  if (!gThread.attached()) {
    if (gTestStarted) {
      resetSoakProgress("detached");
    }
    gLastPartitionValid = false;
    return;
  }

  const uint32_t partitionId = gThread.partitionId();
  if (!gLastPartitionValid) {
    gLastPartitionId = partitionId;
    gLastPartitionValid = true;
    return;
  }

  if (partitionId != gLastPartitionId) {
    gLastPartitionId = partitionId;
    resetSoakProgress("partition-change");
  }
}

void observeIncomingPing(uint16_t seq, uint16_t declaredLength,
                         const otMessageInfo& messageInfo) {
  const Nrf54ThreadExperimental::Role role = gThread.role();
  if (role == Nrf54ThreadExperimental::Role::kLeader) {
    memcpy(&gPeerAddr, &messageInfo.mPeerAddr, sizeof(gPeerAddr));
    gPeerPort = messageInfo.mPeerPort != 0U ? messageInfo.mPeerPort : kUdpPort;
    gPeerKnown = true;

    if (!gObservedUplinkDone &&
        gObservedUplinkIndex < kPayloadSizeCount &&
        declaredLength == kPayloadSizes[gObservedUplinkIndex]) {
      ++gObservedUplinkIndex;
      if (gObservedUplinkIndex >= kPayloadSizeCount) {
        gObservedUplinkDone = true;
        SOAK_PRINT("soak_uplink_observed seq=");
        SOAK_PRINT(seq);
        SOAK_PRINTLN(" complete=1");
      }
    }
    return;
  }

  if ((role == Nrf54ThreadExperimental::Role::kChild ||
       role == Nrf54ThreadExperimental::Role::kRouter) &&
      !gDownlinkReceiverDone &&
      gDownlinkRxIndex < kPayloadSizeCount &&
      declaredLength == kPayloadSizes[gDownlinkRxIndex]) {
    ++gDownlinkRxCount;
    ++gDownlinkRxIndex;
    if (gDownlinkRxIndex >= kPayloadSizeCount) {
      gDownlinkReceiverDone = true;
      SOAK_PRINT("soak_downlink_rx seq=");
      SOAK_PRINT(seq);
      SOAK_PRINTLN(" complete=1");
    }
  }
}

size_t currentPhaseIndex(SoakTxPhase phase) {
  switch (phase) {
    case SoakTxPhase::kDownlink: return gDownlinkIndex;
    case SoakTxPhase::kMulticast: return gMulticastIndex;
    case SoakTxPhase::kUplink:
    default:
      return gUplinkIndex;
  }
}

void advanceCurrentPhase(SoakTxPhase phase) {
  ++gCurrentSeq;
  gRetryCount = 0U;
  gWaitingForAck = false;
  switch (phase) {
    case SoakTxPhase::kDownlink:
      ++gDownlinkIndex;
      if (gDownlinkIndex >= kPayloadSizeCount) {
        gDownlinkDone = true;
        printStatus("downlink-complete");
      }
      break;
    case SoakTxPhase::kMulticast:
      ++gMulticastIndex;
      if (gMulticastIndex >= kPayloadSizeCount) {
        gMulticastDone = true;
        printDone();
      }
      break;
    case SoakTxPhase::kUplink:
    default:
      ++gUplinkIndex;
      if (gUplinkIndex >= kPayloadSizeCount) {
        gUplinkDone = true;
        printStatus("uplink-complete");
      }
      break;
  }
}

void onUdp(void*, const uint8_t* payload, uint16_t length,
           const otMessageInfo& messageInfo) {
  uint8_t type = 0U;
  uint16_t seq = 0U;
  uint16_t declaredLength = 0U;
  if (!parsePayload(payload, length, &type, &seq, &declaredLength)) {
    ++gInvalidRxCount;
    return;
  }

  if (type == kPingType) {
    if (messageInfo.mMulticastLoop) return;
    ++gPingRxCount;
    gLastLength = declaredLength;
    observeIncomingPing(seq, declaredLength, messageInfo);
    if (buildPayload(kAckType, seq, declaredLength) &&
        gThread.sendUdp(messageInfo.mPeerAddr, messageInfo.mPeerPort,
                        gTxBuffer, declaredLength)) {
      ++gAckTxCount;
    }
    return;
  }

  if (type != kAckType) return;
  ++gAckRxCount;
  gLastAckSeq = seq;
  gLastLength = declaredLength;

  if (!gWaitingForAck || seq != gCurrentSeq) return;
  const size_t expectedIdx = currentPhaseIndex(gCurrentTxPhase);
  if (expectedIdx >= kPayloadSizeCount ||
      declaredLength != kPayloadSizes[expectedIdx]) {
    ++gInvalidRxCount;
    return;
  }

  recordResult(gCurrentTxPhase, expectedIdx, 1U);
  advanceCurrentPhase(gCurrentTxPhase);
}

bool sendCurrentPing(SoakTxPhase phase) {
  const size_t idx = currentPhaseIndex(phase);
  if (idx >= kPayloadSizeCount) return false;
  const uint16_t payloadLength = kPayloadSizes[idx];

  otIp6Address destAddr = {};
  if (phase == SoakTxPhase::kMulticast) {
    memcpy(destAddr.mFields.m8, kMulticastAddrBytes,
           sizeof(kMulticastAddrBytes));
  } else if (phase == SoakTxPhase::kDownlink) {
    if (!gPeerKnown) return false;
    memcpy(&destAddr, &gPeerAddr, sizeof(destAddr));
  } else if (!gThread.getLeaderRloc(&destAddr)) {
    return false;
  }

  if (!buildPayload(kPingType, gCurrentSeq, payloadLength)) return false;
  gCurrentTxPhase = phase;
  const uint16_t destPort =
      (phase == SoakTxPhase::kDownlink) ? gPeerPort : kUdpPort;
  const bool ok = gThread.sendUdp(destAddr, destPort, gTxBuffer, payloadLength);
  ++gPingTxCount;
  gLastLength = payloadLength;
  gLastSendMs = millis();
  gWaitingForAck = ok;
  if (!ok) {
    recordResult(phase, idx, 2U);
  }
  return ok;
}

void failCurrentTimeout(SoakTxPhase phase) {
  recordResult(phase, currentPhaseIndex(phase), 4U);
  advanceCurrentPhase(phase);
}

void serviceTxPhase(SoakTxPhase phase) {
  const uint32_t now = millis();
  if (!gWaitingForAck && (now - gLastSendMs) >= kSendIntervalMs) {
    (void)sendCurrentPing(phase);
    return;
  }

  if (gWaitingForAck && gCurrentTxPhase == phase &&
      (now - gLastSendMs) >= kAckTimeoutMs) {
    if (gRetryCount >= kMaxRetriesPerPayload) {
      failCurrentTimeout(phase);
    } else {
      ++gRetryCount;
      ++gRetryTotal;
      (void)sendCurrentPing(phase);
    }
  }
}

void trySubscribeMulticast() {
  if (gMulticastSubscribed || !gThread.udpOpened(kUdpPort)) {
    return;
  }
  otIp6Address mcastAddr = {};
  memcpy(mcastAddr.mFields.m8, kMulticastAddrBytes,
         sizeof(kMulticastAddrBytes));
  gMulticastSubscribed = gThread.subscribeMulticast(mcastAddr);
  if (gMulticastSubscribed) {
    SOAK_PRINTLN("soak_boot mcast_subscribed=1");
  }
}

void exportDatasetOnce() {
  if (gDatasetExported || !gThread.attached()) return;
  size_t hexLen = 0U;
  if (!gThread.exportConfiguredOrActiveDatasetHex(
          gDatasetHex, sizeof(gDatasetHex), &hexLen)) {
    SOAK_PRINTLN("soak_persist_ok=0");
    return;
  }
  gDatasetExported = true;
  SOAK_PRINT("soak_persist_ok=1 dataset_hex=");
  SOAK_PRINTLN(gDatasetHex);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && !Serial1 && (millis() - waitStart) < 1500UL) {
    delay(10);
  }

  otOperationalDataset dataset = {};
  Nrf54ThreadExperimental::buildDemoDataset(&dataset);
  gThread.setActiveDataset(dataset);
  const bool beginOk = gThread.begin(false);
  const bool udpOk = gThread.openUdp(kUdpPort, onUdp, nullptr);

  SOAK_PRINT("soak_boot role=");
  SOAK_PRINT(gThread.roleName());
  SOAK_PRINT(" begin_ok=");
  SOAK_PRINT(beginOk ? 1 : 0);
  SOAK_PRINT(" udp_request=");
  SOAK_PRINTLN(udpOk ? 1 : 0);
}

void loop() {
  gThread.process();
  updateResults();
  trySubscribeMulticast();
  exportDatasetOnce();

  const Nrf54ThreadExperimental::Role currentRole = gThread.role();
  if (currentRole != gLastRole) {
    gLastRole = currentRole;
    printStatus("role");
  }
  trackPartitionAndResetIfNeeded();

  const bool sender =
      currentRole == Nrf54ThreadExperimental::Role::kChild ||
      currentRole == Nrf54ThreadExperimental::Role::kRouter;
  const bool leader = currentRole == Nrf54ThreadExperimental::Role::kLeader;

  if ((sender || leader) && !gTestStarted && gThread.attached() &&
      gThread.udpOpened(kUdpPort)) {
    gTestStarted = true;
    printStatus("test-start");
  }

  if (sender && gTestStarted && !gUplinkDone && !gMulticastStarted) {
    serviceTxPhase(SoakTxPhase::kUplink);
  }

  if (leader && gTestStarted && gObservedUplinkDone && gPeerKnown &&
      !gDownlinkStarted && !gDownlinkDone && !gWaitingForAck) {
    gDownlinkStarted = true;
    gCurrentTxPhase = SoakTxPhase::kDownlink;
    printStatus("downlink-start");
  }

  if (leader && gDownlinkStarted && !gDownlinkDone) {
    serviceTxPhase(SoakTxPhase::kDownlink);
  }

  if (sender && gUplinkDone && gDownlinkReceiverDone && !gMulticastStarted &&
      !gMulticastDone && !gWaitingForAck) {
    gMulticastStarted = true;
    gCurrentTxPhase = SoakTxPhase::kMulticast;
    ++gCurrentSeq;
    printStatus("mcast-start");
  }

  if (sender && gMulticastStarted && !gMulticastDone) {
    serviceTxPhase(SoakTxPhase::kMulticast);
  }

  if (sender && gUplinkDone && !gDownlinkReceiverDone && !gMulticastDone &&
      !gWaitingForAck) {
    gCurrentTxPhase = SoakTxPhase::kDownlink;
  }

  if ((millis() - gLastPrintMs) >= kStatusIntervalMs) {
    gLastPrintMs = millis();
    printStatus("tick");
  }

  const bool localMatrixReady =
      (sender && gUplinkDone && gDownlinkReceiverDone && gMulticastDone) ||
      (leader && gDownlinkDone);
  if (localMatrixReady &&
      ((millis() - gLastFinalMatrixMs) >= kStatusIntervalMs)) {
    gLastFinalMatrixMs = millis();
    printResultMatrix();
  }
}
