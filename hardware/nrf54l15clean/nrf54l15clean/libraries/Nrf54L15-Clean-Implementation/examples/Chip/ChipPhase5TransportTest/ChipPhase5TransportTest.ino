#include <nrf54_all.h>

#include <CHIPError.h>
#include <InetArduino.h>
#include <SystemLayerImplArduino.h>

namespace {

constexpr uint16_t kMatterPort = 5540U;
constexpr uint8_t kMagic = 0xC5U;
constexpr uint8_t kPing = 0x50U;
constexpr uint8_t kAck = 0x41U;
constexpr uint32_t kSendIntervalMs = 500U;
constexpr uint32_t kAckTimeoutMs = 4000U;
constexpr uint32_t kStatusIntervalMs = 2000U;
constexpr uint8_t kMaxRetries = 3U;
constexpr uint16_t kDiscoverySequence = 0U;
constexpr uint16_t kDiscoveryPayloadLength = 8U;
constexpr uint16_t kPayloadSizes[] = {8U, 64U, 512U, 960U, 1200U};
constexpr size_t kPayloadCount = sizeof(kPayloadSizes) / sizeof(kPayloadSizes[0]);
constexpr bool kWipePersistentThreadState = true;
constexpr char kExpectedDatasetHex[] =
    "0E080000000000010001000300000F4A0300000F350600040001000002081122334455"
    "6677880708FD5415C0DE00000005101032547698BADCFE0123456789ABCDEF030A4E72"
    "663534537461676501025D6A0410A54C8D11723F90BE4A6218D4CE07395B0C0402A0"
    "F67B";

#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
constexpr bool kFixedChildRole = true;
constexpr char kRoleMode[] = "child";
#else
constexpr bool kFixedChildRole = false;
constexpr char kRoleMode[] = "leader";
#endif

chip::System::LayerImpl gSystemLayer;
chip::Inet::InetLayer gInetLayer;
xiao_nrf54l15::Nrf54ThreadExperimental gThread;
chip::Inet::UDPEndPointArduino* gEndpoint = nullptr;
chip::Inet::IPAddress gMulticastAddress;
chip::Inet::IPAddress gLeaderAddress;
uint8_t gPayload[1200] = {0};
size_t gPayloadIndex = 0U;
uint16_t gSequence = 1U;
uint8_t gRetryCount = 0U;
uint32_t gLastSendMs = 0U;
uint32_t gLastStatusMs = 0U;
uint32_t gPassCount = 0U;
uint32_t gFailCount = 0U;
uint32_t gLastMleLogCount = UINT32_MAX;
bool gWaitingForAck = false;
bool gWaitingForDiscovery = false;
bool gDiscoveryComplete = false;
bool gDiscoveryFailed = false;
bool gMulticastSubscribed = false;
bool gLeaderAddressReady = false;
bool gDonePrinted = false;
bool gBeginOk = false;
bool gDatasetOk = false;
bool gDatasetMatch = false;
char gDatasetHex[(OT_OPERATIONAL_DATASET_MAX_LENGTH * 2U) + 1U] = {0};

void printCheck(bool passed, const char* name) {
  Serial.print(passed ? "[PASS] " : "[FAIL] ");
  Serial.println(name);
}

void printHexBytes(const uint8_t* data, size_t length) {
  if (data == nullptr) return;
  for (size_t i = 0U; i < length; ++i) {
    if (data[i] < 0x10U) Serial.print('0');
    Serial.print(static_cast<unsigned int>(data[i]), HEX);
  }
}

uint8_t checksum(const uint8_t* data, uint16_t length) {
  uint8_t value = 0U;
  for (uint16_t i = 0U; i < length; ++i) {
    value = static_cast<uint8_t>((value << 1U) | (value >> 7U));
    value ^= data[i];
    value = static_cast<uint8_t>(value + 0x39U);
  }
  return value;
}

bool buildPayload(uint8_t type, uint16_t sequence, uint16_t length) {
  if (length < 8U || length > sizeof(gPayload)) return false;
  memset(gPayload, 0, length);
  gPayload[0] = kMagic;
  gPayload[1] = type;
  gPayload[2] = static_cast<uint8_t>(sequence);
  gPayload[3] = static_cast<uint8_t>(sequence >> 8U);
  gPayload[4] = static_cast<uint8_t>(length);
  gPayload[5] = static_cast<uint8_t>(length >> 8U);
  for (uint16_t i = 6U; i < length - 1U; ++i) {
    gPayload[i] = static_cast<uint8_t>((sequence * 31U) + (i * 17U) + type);
  }
  gPayload[length - 1U] = checksum(gPayload, length - 1U);
  return true;
}

bool parsePayload(const chip::System::PacketBufferHandle& message,
                  uint8_t* type, uint16_t* sequence, uint16_t* length) {
  if (message.IsNull() || message->HasChainedBuffer() || type == nullptr ||
      sequence == nullptr || length == nullptr) {
    return false;
  }
  const size_t dataLength = message->DataLength();
  if (dataLength < 8U || dataLength > sizeof(gPayload)) return false;
  const uint8_t* data = message->Start();
  const uint16_t declaredLength = static_cast<uint16_t>(data[4]) |
      (static_cast<uint16_t>(data[5]) << 8U);
  if (data[0] != kMagic || declaredLength != dataLength ||
      checksum(data, declaredLength - 1U) != data[declaredLength - 1U]) {
    return false;
  }
  *type = data[1];
  *sequence = static_cast<uint16_t>(data[2]) |
      (static_cast<uint16_t>(data[3]) << 8U);
  *length = declaredLength;
  return true;
}

bool sendPayload(const chip::Inet::IPAddress& address, uint16_t port,
                 uint8_t type, uint16_t sequence, uint16_t length) {
  if (gEndpoint == nullptr || !buildPayload(type, sequence, length)) {
    return false;
  }
  chip::System::PacketBufferHandle message =
      chip::System::PacketBufferHandle::NewWithData(gPayload, length);
  if (message.IsNull()) return false;
  return gEndpoint->SendTo(address, port, std::move(message)) == CHIP_NO_ERROR;
}

void onUdp(chip::Inet::UDPEndPoint*,
           chip::System::PacketBufferHandle&& message,
           const chip::Inet::IPPacketInfo& packetInfo) {
  uint8_t type = 0U;
  uint16_t sequence = 0U;
  uint16_t length = 0U;
  if (!parsePayload(message, &type, &sequence, &length)) {
    Serial.println("inet_rx_invalid=1");
    return;
  }

  if (type == kPing &&
      gThread.role() ==
          xiao_nrf54l15::Nrf54ThreadExperimental::Role::kLeader) {
    const bool sent = sendPayload(packetInfo.mAddress, packetInfo.mPort, kAck,
                                  sequence, length);
    Serial.print("inet_reply len=");
    Serial.print(length);
    Serial.print(" ok=");
    Serial.println(sent ? 1 : 0);
    return;
  }

  if (type == kAck && gWaitingForDiscovery &&
      sequence == kDiscoverySequence && length == kDiscoveryPayloadLength) {
    gLeaderAddress = packetInfo.mAddress;
    gLeaderAddressReady = true;
    gDiscoveryComplete = true;
    gWaitingForDiscovery = false;
    gRetryCount = 0U;
    Serial.println("inet_discovery_pass mode=multicast");
    return;
  }

  if (type == kAck && gWaitingForAck && gPayloadIndex < kPayloadCount &&
      sequence == gSequence && length == kPayloadSizes[gPayloadIndex]) {
    Serial.print("inet_pass len=");
    Serial.print(length);
    Serial.println(" mode=unicast");
    ++gPassCount;
    ++gPayloadIndex;
    ++gSequence;
    gRetryCount = 0U;
    gWaitingForAck = false;
  }
}

void printStatus() {
  xiao_nrf54l15::Nrf54ThreadExperimental::AttachDiagnostics attach = {};
  const bool attachOk = gThread.getAttachDiagnostics(&attach);
  Serial.print("inet_status role=");
  Serial.print(gThread.roleName());
  Serial.print(" rloc16=0x");
  Serial.print(gThread.rloc16(), HEX);
  Serial.print(" part=0x");
  Serial.print(gThread.partitionId(), HEX);
  Serial.print(" udp=");
  Serial.print(gThread.udpOpened(kMatterPort) ? 1 : 0);
  Serial.print(" subscribed=");
  Serial.print(gMulticastSubscribed ? 1 : 0);
  Serial.print(" leader_rloc_ready=");
  Serial.print(gLeaderAddressReady ? 1 : 0);
  Serial.print(" discovery=");
  Serial.print(gDiscoveryComplete ? 1 : 0);
  Serial.print(" begin_ok=");
  Serial.print(gBeginOk ? 1 : 0);
  Serial.print(" role_mode=");
  Serial.print(kRoleMode);
  Serial.print(" wipe=");
  Serial.print(kWipePersistentThreadState ? 1 : 0);
  Serial.print(" dataset_ok=");
  Serial.print(gDatasetOk ? 1 : 0);
  Serial.print(" dataset_match=");
  Serial.print(gDatasetMatch ? 1 : 0);
  Serial.print(" fallback_used=");
  Serial.print(attachOk && attach.childFirstFallbackUsed ? 1 : 0);
  Serial.print(" attach_attempts=");
  Serial.print(attachOk ? attach.attachAttempts : 0U);
  Serial.print(" dataset_hex=");
  Serial.print(gDatasetHex[0] != '\0' ? gDatasetHex : "unavailable");
  Serial.print(" pass=");
  Serial.print(gPassCount);
  Serial.print(" fail=");
  Serial.println(gFailCount);
}

void printRadioStatus() {
  xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot radio = {};
  if (!xiao_nrf54l15::OpenThreadPlatformSkeleton::snapshot(&radio)) {
    Serial.println("inet_radio snapshot=0");
    return;
  }

  Serial.print("inet_radio snapshot=1 state=");
  Serial.print(static_cast<unsigned int>(radio.radioState));
  Serial.print(" enabled=");
  Serial.print(radio.radioEnabled ? 1 : 0);
  Serial.print(" ready=");
  Serial.print(radio.radioBackendReady ? 1 : 0);
  Serial.print(" ch=");
  Serial.print(radio.radioChannel);
  Serial.print(" rx_idle=");
  Serial.print(radio.radioRxOnWhenIdle ? 1 : 0);
  Serial.print(" pan=0x");
  Serial.print(radio.panId, HEX);
  Serial.print(" short=0x");
  Serial.print(radio.shortAddress, HEX);
  Serial.print(" proc=");
  Serial.print(radio.processCount);
  Serial.print(" tx_req=");
  Serial.print(radio.txRequestCount);
  Serial.print(" tx_done=");
  Serial.print(radio.radioTxDoneCount);
  Serial.print(" tx_err=");
  Serial.print(radio.radioLastError);
  Serial.print(" tx_ack=");
  Serial.print(radio.radioLastTxAcked ? 1 : 0);
  Serial.print(" tx_len=");
  Serial.print(radio.radioLastTxLength);
  Serial.print(" tx_seq=");
  Serial.print(radio.radioLastTxSequence);
  Serial.print(" tx_type=");
  Serial.print(radio.radioLastTxFrameType);
  Serial.print(" tx_dstmode=");
  Serial.print(radio.radioLastTxDstAddrMode);
  Serial.print(" tx_hdr=");
  printHexBytes(radio.radioLastTxHeader, sizeof(radio.radioLastTxHeader));
  Serial.println();

  Serial.print("inet_radio_rx poll=");
  Serial.print(radio.radioReceivePollCount);
  Serial.print(" done=");
  Serial.print(radio.radioRxDoneCount);
  Serial.print(" filter=");
  Serial.print(radio.radioFilteredCount);
  Serial.print(" crc=");
  Serial.print(radio.radioRxCrcErrorCount);
  Serial.print(" invalid=");
  Serial.print(radio.radioRxInvalidLengthCount);
  Serial.print(" phr=");
  Serial.print(radio.radioLastRxPhr);
  Serial.print(" rejected_len=");
  Serial.print(radio.radioLastRejectedLength);
  Serial.print(" len=");
  Serial.print(radio.radioLastRxLength);
  Serial.print(" seq=");
  Serial.print(radio.radioLastRxSequence);
  Serial.print(" type=");
  Serial.print(radio.radioLastRxFrameType);
  Serial.print(" dstmode=");
  Serial.print(radio.radioLastRxDstAddrMode);
  Serial.print(" rssi=");
  Serial.print(radio.lastRssiDbm);
  Serial.print(" hdr=");
  printHexBytes(radio.radioLastRxHeader, sizeof(radio.radioLastRxHeader));
  Serial.print(" queue=");
  Serial.print(radio.radioRxQueueDepth);
  Serial.print('/');
  Serial.print(radio.radioRxQueueHighWater);
  Serial.print(" overflow=");
  Serial.print(radio.radioRxQueueOverflowCount);
  Serial.print(" receive_at=");
  Serial.print(radio.radioReceiveAtPending ? 1 : 0);
  Serial.print('/');
  Serial.print(radio.radioReceiveAtActive ? 1 : 0);
  Serial.print('/');
  Serial.print(radio.radioReceiveAtScheduleCount);
  Serial.print('/');
  Serial.print(radio.radioReceiveAtStartCount);
  Serial.print('/');
  Serial.print(radio.radioReceiveAtTimeoutCount);
  Serial.print('/');
  Serial.println(radio.radioReceiveAtLateCount);

  Serial.print("inet_thread_core valid=");
  Serial.print(radio.threadCoreDebugValid ? 1 : 0);
  Serial.print(" attaching=");
  Serial.print(radio.threadAttachInProgress ? 1 : 0);
  Serial.print(" response=");
  Serial.print(radio.threadReceivedResponseFromParent ? 1 : 0);
  Serial.print(" state=");
  Serial.print(radio.threadAttachStateName);
  Serial.print('/');
  Serial.print(radio.threadAttachState);
  Serial.print(" mode=");
  Serial.print(radio.threadAttachModeName);
  Serial.print('/');
  Serial.print(radio.threadAttachMode);
  Serial.print(" reattach=");
  Serial.print(radio.threadReattachModeName);
  Serial.print('/');
  Serial.print(radio.threadReattachMode);
  Serial.print(" parent_state=");
  Serial.print(radio.threadParentCandidateStateName);
  Serial.print('/');
  Serial.print(radio.threadParentCandidateState);
  Serial.print(" parent_rloc=0x");
  Serial.print(radio.threadParentCandidateRloc16, HEX);
  Serial.print(" parent_req=");
  Serial.print(radio.threadParentRequestCounter);
  Serial.print(" child_id_left=");
  Serial.print(radio.threadChildIdRequestsRemaining);
  Serial.print(" attach_count=");
  Serial.print(radio.threadAttachCounter);
  Serial.print(" timer_ms=");
  Serial.print(radio.threadAttachTimerRemainingMs);
  Serial.print(" mle_logs=");
  Serial.println(radio.recentMleLogCount);

  if (gLastMleLogCount != radio.recentMleLogCount) {
    gLastMleLogCount = radio.recentMleLogCount;
    for (size_t index = 0U;
         index < xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot::
                     kRecentLogLineCount;
         ++index) {
      if (radio.recentMleLogLines[index][0] == '\0') continue;
      Serial.print("inet_mle_log index=");
      Serial.print(index);
      Serial.print(" text=");
      Serial.println(radio.recentMleLogLines[index]);
    }
  }
}

void serviceSender() {
  using Role = xiao_nrf54l15::Nrf54ThreadExperimental::Role;
  const Role role = gThread.role();
  const bool sender = role == Role::kChild || role == Role::kRouter;
  if (!sender || !gThread.udpOpened(kMatterPort) ||
      !gMulticastSubscribed || gDiscoveryFailed ||
      gPayloadIndex >= kPayloadCount) {
    return;
  }

  const uint32_t now = millis();
  if (!gDiscoveryComplete) {
    if (gWaitingForDiscovery) {
      if (now - gLastSendMs < kAckTimeoutMs) return;
      if (++gRetryCount > kMaxRetries) {
        Serial.println("inet_discovery_fail reason=timeout");
        ++gFailCount;
        gDiscoveryFailed = true;
        gWaitingForDiscovery = false;
        return;
      }
    } else if (now - gLastSendMs < kSendIntervalMs) {
      return;
    }

    if (!sendPayload(gMulticastAddress, kMatterPort, kPing,
                     kDiscoverySequence, kDiscoveryPayloadLength)) {
      Serial.println("inet_discovery_fail reason=send");
      ++gFailCount;
      gDiscoveryFailed = true;
      return;
    }
    gLastSendMs = now;
    gWaitingForDiscovery = true;
    Serial.print("inet_discovery_send mode=multicast retry=");
    Serial.println(gRetryCount);
    return;
  }

  if (!gLeaderAddressReady) return;

  if (gWaitingForAck) {
    if (now - gLastSendMs < kAckTimeoutMs) return;
    if (++gRetryCount > kMaxRetries) {
      Serial.print("inet_fail len=");
      Serial.print(kPayloadSizes[gPayloadIndex]);
      Serial.println(" mode=unicast reason=timeout");
      ++gFailCount;
      ++gPayloadIndex;
      ++gSequence;
      gRetryCount = 0U;
      gWaitingForAck = false;
      return;
    }
  } else if (now - gLastSendMs < kSendIntervalMs) {
    return;
  }

  const uint16_t length = kPayloadSizes[gPayloadIndex];
  if (!sendPayload(gLeaderAddress, kMatterPort, kPing, gSequence, length)) {
    Serial.print("inet_fail len=");
    Serial.print(length);
    Serial.println(" mode=unicast reason=send");
    ++gFailCount;
    ++gPayloadIndex;
    ++gSequence;
    gRetryCount = 0U;
    gWaitingForAck = false;
    return;
  }
  gLastSendMs = now;
  gWaitingForAck = true;
  Serial.print("inet_send len=");
  Serial.print(length);
  Serial.print(" mode=unicast");
  Serial.print(" retry=");
  Serial.println(gRetryCount);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) delay(10);

  Serial.println("=== CHIP Inet Two-Board Transport Test ===");
  CHIP_ERROR error = gSystemLayer.Init();
  printCheck(error == CHIP_NO_ERROR && gSystemLayer.IsInitialized(),
             "System layer initialization");

  gInetLayer.SetThreadTransport(gThread);
  if (error == CHIP_NO_ERROR) error = gInetLayer.Init(gSystemLayer);
  printCheck(error == CHIP_NO_ERROR && gInetLayer.IsInitialized(),
             "Inet layer initialization");

  chip::Inet::InterfaceId interfaceId;
  error = chip::Inet::IPAddress::FromString(
      "ff03::1", gMulticastAddress, interfaceId);
  printCheck(error == CHIP_NO_ERROR && gMulticastAddress.IsMulticast(),
             "IPv6 multicast parsing");

  if (error == CHIP_NO_ERROR) error = gInetLayer.NewUDPEndPoint(&gEndpoint);
  if (error == CHIP_NO_ERROR) {
    gEndpoint->SetReceiveCallback(onUdp, nullptr);
    error = gEndpoint->Bind(chip::Inet::IPAddress::Any, kMatterPort);
  }
  if (error == CHIP_NO_ERROR) error = gEndpoint->Listen();
  printCheck(error == CHIP_NO_ERROR && gEndpoint != nullptr &&
                 gEndpoint->IsListening(),
             "Queued OpenThread UDP bind and listen");

  chip::System::PacketBufferHandle maximumDatagram =
      chip::System::PacketBufferHandle::New(1280U);
  chip::System::PacketBufferHandle oversizedDatagram =
      chip::System::PacketBufferHandle::New(1281U);
  printCheck(!maximumDatagram.IsNull() && oversizedDatagram.IsNull(),
             "IPv6 minimum-MTU packet-buffer capacity");

  static const uint8_t kProbe[] = {'C', 'H', 'I', 'P'};
  chip::System::PacketBufferHandle preStartMessage =
      chip::System::PacketBufferHandle::NewWithData(kProbe, sizeof(kProbe));
  error = gEndpoint != nullptr
      ? gEndpoint->SendTo(gMulticastAddress, kMatterPort,
                          std::move(preStartMessage))
      : CHIP_ERROR_INCORRECT_STATE;
  printCheck(error != CHIP_NO_ERROR, "UDP send fails closed before Thread start");

  const bool datasetImported = gThread.setActiveDatasetHex(kExpectedDatasetHex);
  size_t datasetHexLength = 0U;
  const bool datasetExported = datasetImported &&
      gThread.exportConfiguredOrActiveDatasetHex(
          gDatasetHex, sizeof(gDatasetHex), &datasetHexLength);
  gDatasetMatch = datasetExported &&
      datasetHexLength == (sizeof(kExpectedDatasetHex) - 1U) &&
      strcmp(gDatasetHex, kExpectedDatasetHex) == 0;
  gDatasetOk = datasetImported && gDatasetMatch;
  const bool beginOk = gDatasetOk &&
      (kFixedChildRole
           ? gThread.beginAsChild(kWipePersistentThreadState)
           : gThread.beginAsRouter(kWipePersistentThreadState));
  gBeginOk = beginOk;
  Serial.print("inet_boot dataset_ok=");
  Serial.print(gDatasetOk ? 1 : 0);
  Serial.print(" dataset_match=");
  Serial.print(gDatasetMatch ? 1 : 0);
  Serial.print(" role_mode=");
  Serial.print(kRoleMode);
  Serial.print(" wipe=");
  Serial.print(kWipePersistentThreadState ? 1 : 0);
  Serial.print(" dataset_hex=");
  Serial.print(gDatasetHex[0] != '\0' ? gDatasetHex : "unavailable");
  Serial.print(" begin_ok=");
  Serial.println(beginOk ? 1 : 0);
}

void loop() {
  gInetLayer.Service();
  (void)gSystemLayer.HandleEvents();

  if (gThread.attached() && !gMulticastSubscribed) {
    otIp6Address address = {};
    memcpy(address.mFields.m8, gMulticastAddress.mAddr, sizeof(address.mFields.m8));
    gMulticastSubscribed = gThread.subscribeMulticast(address);
  }

  serviceSender();
  if ((gPayloadIndex >= kPayloadCount || gDiscoveryFailed) &&
      !gWaitingForAck && !gWaitingForDiscovery && !gDonePrinted) {
    gDonePrinted = true;
    Serial.print("inet_done pass=");
    Serial.print(gPassCount);
    Serial.print(" fail=");
    Serial.println(gFailCount);
  }

  const uint32_t now = millis();
  if (now - gLastStatusMs >= kStatusIntervalMs) {
    gLastStatusMs = now;
    printStatus();
    printRadioStatus();
  }
  delay(1);
}
