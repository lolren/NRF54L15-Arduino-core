/*
 * BleChannelSoundingInitiator
 *
 * Runs a Bluetooth LE Channel Sounding Test as the initiator, receives the
 * reflector's controller step data, and prints a phase-based range estimate.
 * Flash BleChannelSoundingReflector on the peer board.
 *
 * Select the 128 MHz CPU menu option. A CRC-framed request establishes a
 * per-cycle token before Nordic SDC/MPSL owns RADIO for the CS Test. The same
 * token correlates the peer result returned afterward. This is not a connected
 * ACL CS link.
 */

#include <Arduino.h>

#include <math.h>
#include <string.h>

#include "ble_channel_sounding.h"
#include "ble_cs_controller_runtime.h"
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
constexpr uint32_t kControllerTimeoutMs = 8000U;
constexpr uint32_t kControllerStopTimeoutMs = 1500U;
constexpr uint32_t kPeerTransferTimeoutMs = 5000U;
constexpr uint32_t kSessionStartGuardMs = 220U;
constexpr uint32_t kRetryDelayMs = 173U;
constexpr uint8_t kRequiredMode0Steps = 3U;
constexpr uint32_t kPeerProfileTag = 0x43534D32UL;  // "CSM2"

struct ControllerCapture {
  BleCsSubeventResultHeader header{};
  uint8_t stepData[kBleCsMaxControllerStepDataBytes] = {0};
  uint16_t stepDataLen = 0U;
  uint16_t initialEvents = 0U;
  uint16_t continuationEvents = 0U;
  uint16_t rejectedEvents = 0U;
  uint32_t droppedPackets = 0U;
  int32_t runtimeError = 0;
  bool complete = false;
};

BleCsControllerRuntime gController;
BleCsSubeventResultReassembler gReassembler;
BleChannelSoundingRadio gTransferRadio;
BleCsControllerTestConfig gTestConfig;
BleCsPeerSession gSession;
ControllerCapture gLocalCapture;
uint8_t gPeerTransferData[kBleCsMaxControllerStepDataBytes] = {0};
uint32_t gCycle = 0U;
uint32_t gGoodRanges = 0U;
uint32_t gLastAcceptedSessionToken = 0U;
uint32_t gSessionPrngState = 0U;

void led(bool on) {
  (void)Gpio::write(kPinUserLed, !on);
}

[[noreturn]] void fail(uint8_t stage) {
  Serial.print(F("fatal_stage="));
  Serial.println(stage);
  while (true) {
    led(true);
    delay(80U + (static_cast<uint32_t>(stage) * 20U));
    led(false);
    delay(400U);
  }
}

void configureBoard() {
  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput,
                        GpioPull::kDisabled);
  led(false);
  Serial.begin(115200);
  const uint32_t startedMs = millis();
  while (!Serial && (millis() - startedMs) < 1500U) {
  }
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  if (!BoardControl::enableRfPath(kAntennaPath)) {
    fail(1U);
  }
  if (nrf54l15_core_get_cpu_frequency_hz() != 128000000UL) {
    Serial.println(F("channel_sounding_error=requires_cpu_128mhz"));
    fail(2U);
  }
}

void drainControllerPackets(ControllerCapture* capture) {
  BleCsControllerHciPacket packet{};
  while (gController.readPacket(&packet)) {
    if (capture == nullptr || packet.messageType != 0x04U) {
      continue;
    }

    BleCsHciLeMetaEvent meta{};
    if (!BleChannelSoundingRadio::parseHciLeMetaEvent(
            packet.data, packet.dataLength, &meta)) {
      continue;
    }

    BleCsSubeventResult result{};
    bool accepted = false;
    if (meta.subeventCode == kBleCsHciEvtSubeventResult) {
      ++capture->initialEvents;
      accepted = gReassembler.consumeInitialEvent(meta.payload,
                                                  meta.payloadLen, &result);
    } else if (meta.subeventCode ==
               kBleCsHciEvtSubeventResultContinue) {
      ++capture->continuationEvents;
      accepted = gReassembler.consumeContinuationEvent(
          meta.payload, meta.payloadLen, &result);
    } else {
      continue;
    }

    if (!accepted) {
      ++capture->rejectedEvents;
      continue;
    }
    if (!result.isComplete) {
      continue;
    }
    if (result.stepData == nullptr || result.stepDataLen == 0U ||
        result.stepDataLen > sizeof(capture->stepData)) {
      ++capture->rejectedEvents;
      continue;
    }

    capture->header = result.header;
    capture->stepDataLen = result.stepDataLen;
    memcpy(capture->stepData, result.stepData, result.stepDataLen);
    capture->complete = true;
  }
}

bool stopTimedOutController() {
  if (!gController.testRunning()) {
    return true;
  }
  if (!gController.stopTest()) {
    return false;
  }

  const uint32_t startedMs = millis();
  while (gController.testRunning() &&
         (millis() - startedMs) < kControllerStopTimeoutMs) {
    gController.poll();
    drainControllerPackets(nullptr);
  }
  return !gController.testRunning();
}

bool runControllerTest(ControllerCapture* capture) {
  if (capture == nullptr) {
    return false;
  }
  *capture = ControllerCapture{};
  gReassembler.reset();

  if (!gController.begin()) {
    capture->runtimeError = gController.lastError();
    return false;
  }
  if (!gController.startTest(BleCsControllerRole::kInitiator, gTestConfig)) {
    capture->runtimeError = gController.lastError();
    (void)gController.end();
    return false;
  }

  const uint32_t startedMs = millis();
  while (!capture->complete &&
         (millis() - startedMs) < kControllerTimeoutMs) {
    gController.poll();
    drainControllerPackets(capture);
  }

  bool stopped = true;
  if (!capture->complete) {
    stopped = stopTimedOutController();
  }
  capture->droppedPackets = gController.droppedPacketCount();
  capture->runtimeError = gController.lastError();
  const bool ended = gController.end();

  return stopped && ended && capture->complete &&
         capture->droppedPackets == 0U &&
         capture->rejectedEvents == 0U &&
         capture->header.procedureDoneStatus ==
             kBleCsProcedureDoneComplete &&
         capture->header.subeventDoneStatus == kBleCsSubeventDoneComplete &&
         capture->header.numAntennaPaths <= 1U &&
         capture->header.numStepsReported > kRequiredMode0Steps;
}

bool beginPeerTransport() {
  BleCsConfig config{};
  config.txPowerDbm = 0;
  config.maxPayloadLength = 255U;
  config.enableRtt = false;
  config.enableRawDfeCapture = false;
  return gTransferRadio.begin(config);
}

uint16_t transferIdForSession(const BleCsPeerSession& session) {
  uint16_t transferId = static_cast<uint16_t>(
      session.token ^ (session.token >> 16U));
  return (transferId != 0U) ? transferId : 1U;
}

bool beginCorrelatedSession(BleCsStepTransferStats* outTransfer) {
  if (outTransfer == nullptr) {
    return false;
  }
  *outTransfer = BleCsStepTransferStats{};

  if (gSessionPrngState == 0U) {
    const uint64_t deviceId = Ficr::deviceId();
    gSessionPrngState = static_cast<uint32_t>(deviceId) ^
                        static_cast<uint32_t>(deviceId >> 32U) ^ micros() ^
                        0xA5C35A7DUL;
  }
  gSessionPrngState ^= gSessionPrngState << 13U;
  gSessionPrngState ^= gSessionPrngState >> 17U;
  gSessionPrngState ^= gSessionPrngState << 5U;
  uint32_t token = gSessionPrngState ^ (gCycle * 0x9E3779B9UL);
  if (token == 0U || token == gLastAcceptedSessionToken) {
    token = 0x43530000UL ^ micros() ^ (gCycle * 0x45D9F3BUL);
  }
  if (token == 0U || token == gLastAcceptedSessionToken) {
    token ^= 0xA5A55A5AUL;
  }

  gSession = BleCsPeerSession{};
  gSession.token = token;
  gSession.profileTag = kPeerProfileTag;
  gSession.drbgNonce = static_cast<uint16_t>(token ^ (token >> 16U));
  if (gSession.drbgNonce == 0U) {
    gSession.drbgNonce = 1U;
  }
  gTestConfig.drbgNonce = gSession.drbgNonce;

  uint8_t request[kBleCsPeerSessionRequestBytes] = {0};
  uint16_t requestLen = 0U;
  if (!BleChannelSoundingRadio::encodePeerSessionRequest(
          gSession, request, sizeof(request), &requestLen) ||
      !beginPeerTransport()) {
    return false;
  }
  const bool sent = gTransferRadio.sendPeerStepData(
      request, requestLen, transferIdForSession(gSession),
      kPeerTransferTimeoutMs, outTransfer);
  gTransferRadio.end();
  if (sent) {
    // The receiver lingers briefly to recover a lost final ACK before it
    // releases RADIO and starts SDC.
    delay(kSessionStartGuardMs);
  }
  return sent;
}

struct StepCounter {
  uint16_t count = 0U;
};

bool countStep(const BleCsSubeventStep*, void* userData) {
  StepCounter* counter = static_cast<StepCounter*>(userData);
  if (counter == nullptr || counter->count == 0xFFFFU) {
    return false;
  }
  ++counter->count;
  return true;
}

void printRetry(const ControllerCapture& capture) {
  Serial.print(F("cs_result role=initiator result=RETRY cycle="));
  Serial.print(gCycle);
  Serial.print(F(" proc_status="));
  Serial.print(capture.header.procedureDoneStatus);
  Serial.print(F(" subevent_status="));
  Serial.print(capture.header.subeventDoneStatus);
  Serial.print(F(" steps="));
  Serial.print(capture.header.numStepsReported);
  Serial.print(F(" bytes="));
  Serial.print(capture.stepDataLen);
  Serial.print(F(" hci_initial="));
  Serial.print(capture.initialEvents);
  Serial.print(F(" hci_continue="));
  Serial.print(capture.continuationEvents);
  Serial.print(F(" rejected="));
  Serial.print(capture.rejectedEvents);
  Serial.print(F(" dropped="));
  Serial.print(capture.droppedPackets);
  Serial.print(F(" error="));
  Serial.println(capture.runtimeError);
}

void printRange(const BleCsEstimate& estimate,
                const ControllerCapture& capture,
                uint16_t peerStepCount,
                uint16_t peerStepDataLen,
                uint16_t transferId,
                const BleCsStepTransferStats& transfer) {
  Serial.print(F("cs_result role=initiator result=PASS cycle="));
  Serial.print(gCycle);
  Serial.print(F(" good_ranges="));
  Serial.print(gGoodRanges);
  Serial.print(F(" sounding=bluetooth_le_cs_test local_steps="));
  Serial.print(capture.header.numStepsReported);
  Serial.print(F(" peer_steps="));
  Serial.print(peerStepCount);
  Serial.print(F(" local_bytes="));
  Serial.print(capture.stepDataLen);
  Serial.print(F(" peer_bytes="));
  Serial.print(peerStepDataLen);
  Serial.print(F(" transfer_id="));
  Serial.print(transferId);
  Serial.print(F(" session_token=0x"));
  Serial.print(gSession.token, HEX);
  Serial.print(F(" transfer_crc32=0x"));
  Serial.print(transfer.payloadCrc32, HEX);
  Serial.print(F(" transfer_frames="));
  Serial.print(transfer.framesReceived);
  Serial.print(F(" transfer_duplicates="));
  Serial.print(transfer.duplicateFrames);
  Serial.print(F(" hci_initial="));
  Serial.print(capture.initialEvents);
  Serial.print(F(" hci_continue="));
  Serial.print(capture.continuationEvents);
  Serial.print(F(" rejected="));
  Serial.print(capture.rejectedEvents);
  Serial.print(F(" dropped="));
  Serial.print(capture.droppedPackets);
  Serial.print(F(" used_channels="));
  Serial.print(estimate.usedChannels);
  Serial.print(F(" rtt_channels="));
  Serial.print(estimate.rttChannels);
  Serial.print(F(" pbr_m="));
  Serial.print(estimate.phaseSlopeDistanceMeters, 4U);
  Serial.print(F(" rtt_m="));
  if (isfinite(estimate.rttDistanceMeters)) {
    Serial.print(estimate.rttDistanceMeters, 4U);
  } else {
    Serial.print(F("na"));
  }
  Serial.print(F(" distance_m="));
  Serial.println(estimate.distanceMeters, 4U);
}

}  // namespace

void setup() {
  configureBoard();
  Serial.println(F("CoreBleChannelSoundingInitiator start"));
  Serial.println(F("role=initiator sounding=bluetooth_le_cs_test cpu_mhz=128"));
  Serial.println(F("controller=nordic_sdc result_transport=crc_radio"));
  Serial.println(F("connected_acl_cs=0 qualification_claimed=0"));
  delay(500U);
}

void loop() {
  ++gCycle;
  BleCsStepTransferStats sessionTransfer{};
  if (!beginCorrelatedSession(&sessionTransfer)) {
    Serial.print(F("cs_result role=initiator result=RETRY cycle="));
    Serial.print(gCycle);
    Serial.print(F(" reason=session_sync bytes="));
    Serial.print(sessionTransfer.bytesTransferred);
    Serial.print(F(" retries="));
    Serial.println(sessionTransfer.retries);
    delay(kRetryDelayMs);
    return;
  }
  if (!runControllerTest(&gLocalCapture)) {
    printRetry(gLocalCapture);
    delay(kRetryDelayMs);
    return;
  }

  if (!beginPeerTransport()) {
    Serial.print(F("cs_result role=initiator result=RETRY cycle="));
    Serial.print(gCycle);
    Serial.println(F(" reason=transport_begin"));
    delay(kRetryDelayMs);
    return;
  }

  uint16_t peerTransferDataLen = 0U;
  uint16_t transferId = 0U;
  BleCsStepTransferStats transfer{};
  const bool received = gTransferRadio.receivePeerStepData(
      gPeerTransferData, sizeof(gPeerTransferData), &peerTransferDataLen,
      &transferId,
      kPeerTransferTimeoutMs, &transfer);
  gTransferRadio.end();
  if (!received) {
    Serial.print(F("cs_result role=initiator result=RETRY cycle="));
    Serial.print(gCycle);
    Serial.print(F(" reason=peer_transfer bytes="));
    Serial.print(transfer.bytesTransferred);
    Serial.print(F(" rejected="));
    Serial.println(transfer.rejectedFrames);
    delay(kRetryDelayMs);
    return;
  }

  BleCsPeerResultMetadata peerMetadata{};
  const uint8_t* peerStepData = nullptr;
  uint16_t peerStepDataLen = 0U;
  const bool decoded = BleChannelSoundingRadio::decodePeerResultEnvelope(
      gPeerTransferData, peerTransferDataLen, &peerMetadata, &peerStepData,
      &peerStepDataLen);
  const bool metadataMatches =
      decoded && transferId == transferIdForSession(gSession) &&
      peerMetadata.session.token == gSession.token &&
      peerMetadata.session.profileTag == gSession.profileTag &&
      peerMetadata.session.drbgNonce == gSession.drbgNonce &&
      peerMetadata.role ==
          static_cast<uint8_t>(BleCsControllerRole::kReflector) &&
      peerMetadata.configId == gLocalCapture.header.configId &&
      peerMetadata.startAclConnEventCounter ==
          gLocalCapture.header.startAclConnEventCounter &&
      peerMetadata.procedureCounter ==
          gLocalCapture.header.procedureCounter &&
      peerMetadata.numAntennaPaths ==
          gLocalCapture.header.numAntennaPaths &&
      peerMetadata.mainModeType == gTestConfig.mainModeType &&
      peerMetadata.subModeType == gTestConfig.subModeType &&
      peerMetadata.rttType == gTestConfig.rttType;
  if (!metadataMatches) {
    Serial.print(F("cs_result role=initiator result=RETRY cycle="));
    Serial.print(gCycle);
    Serial.print(F(" reason=peer_metadata decoded="));
    Serial.print(decoded ? 1 : 0);
    Serial.print(F(" transfer_id="));
    Serial.print(transferId);
    Serial.print(F(" expected_id="));
    Serial.println(transferIdForSession(gSession));
    delay(kRetryDelayMs);
    return;
  }

  StepCounter peerSteps{};
  BleCsEstimate estimate{};
  const bool peerParsed = BleChannelSoundingRadio::parseSubeventStepData(
      peerStepData, peerStepDataLen, countStep, &peerSteps);
  const bool estimated =
      peerParsed && peerSteps.count == peerMetadata.numStepsReported &&
      BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
          gLocalCapture.stepData, gLocalCapture.stepDataLen, peerStepData,
          peerStepDataLen, true, &estimate);
  if (!estimated || !estimate.valid || !isfinite(estimate.distanceMeters) ||
      !isfinite(estimate.phaseSlopeDistanceMeters) ||
      estimate.usedChannels < 4U) {
    Serial.print(F("cs_result role=initiator result=RETRY cycle="));
    Serial.print(gCycle);
    Serial.print(F(" reason=estimate peer_parsed="));
    Serial.print(peerParsed ? 1 : 0);
    Serial.print(F(" used_channels="));
    Serial.println(estimate.usedChannels);
    delay(kRetryDelayMs);
    return;
  }

  gLastAcceptedSessionToken = gSession.token;
  ++gGoodRanges;
  led(true);
  printRange(estimate, gLocalCapture, peerSteps.count, peerStepDataLen,
             transferId, transfer);
  delay(20U);
  led(false);
  delay(kRetryDelayMs);
}
