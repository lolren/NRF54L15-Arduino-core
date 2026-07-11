/*
 * BleChannelSoundingReflector
 *
 * Runs a Bluetooth LE Channel Sounding Test as the reflector, then sends its
 * completed controller step data to BleChannelSoundingInitiator.
 *
 * Select the 128 MHz CPU menu option. A CRC-framed request establishes a
 * per-cycle token before Nordic SDC/MPSL owns RADIO for the CS Test. The same
 * token correlates the result returned afterward. This is not a connected ACL
 * CS link.
 */

#include <Arduino.h>

#include <string.h>

#include "ble_channel_sounding.h"
#include "ble_cs_controller_runtime.h"
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
constexpr uint32_t kControllerTimeoutMs = 12000U;
constexpr uint32_t kControllerStopTimeoutMs = 1500U;
constexpr uint32_t kPeerTransferTimeoutMs = 5000U;
constexpr uint32_t kResultSendTimeoutMs = 2500U;
constexpr uint32_t kRetryDelayMs = 227U;
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
ControllerCapture gCapture;
uint8_t gPeerEnvelope[kBleCsMaxControllerStepDataBytes] = {0};
uint32_t gCycle = 0U;
uint32_t gTransferredResults = 0U;

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
  if (!gController.startTest(BleCsControllerRole::kReflector, gTestConfig)) {
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

bool awaitCorrelatedSession(BleCsStepTransferStats* outTransfer) {
  if (outTransfer == nullptr) {
    return false;
  }
  *outTransfer = BleCsStepTransferStats{};
  if (!beginPeerTransport()) {
    return false;
  }

  uint8_t request[kBleCsPeerSessionRequestBytes] = {0};
  uint16_t requestLen = 0U;
  uint16_t transferId = 0U;
  const bool received = gTransferRadio.receivePeerStepData(
      request, sizeof(request), &requestLen, &transferId,
      kPeerTransferTimeoutMs, outTransfer);
  gTransferRadio.end();
  if (!received || !BleChannelSoundingRadio::decodePeerSessionRequest(
                       request, requestLen, &gSession) ||
      gSession.profileTag != kPeerProfileTag ||
      transferId != transferIdForSession(gSession)) {
    gSession = BleCsPeerSession{};
    return false;
  }

  gTestConfig.drbgNonce = gSession.drbgNonce;
  return true;
}

void printRetry(const ControllerCapture& capture) {
  Serial.print(F("cs_result role=reflector result=RETRY cycle="));
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

void printTransfer(const ControllerCapture& capture,
                   const BleCsStepTransferStats& transfer) {
  Serial.print(F("cs_result role=reflector result=PASS cycle="));
  Serial.print(gCycle);
  Serial.print(F(" transferred_results="));
  Serial.print(gTransferredResults);
  Serial.print(F(" sounding=bluetooth_le_cs_test steps="));
  Serial.print(capture.header.numStepsReported);
  Serial.print(F(" bytes="));
  Serial.print(capture.stepDataLen);
  Serial.print(F(" transfer_id="));
  Serial.print(transfer.transferId);
  Serial.print(F(" session_token=0x"));
  Serial.print(gSession.token, HEX);
  Serial.print(F(" transfer_crc32=0x"));
  Serial.print(transfer.payloadCrc32, HEX);
  Serial.print(F(" transfer_frames="));
  Serial.print(transfer.framesSent);
  Serial.print(F(" transfer_acks="));
  Serial.print(transfer.acknowledgements);
  Serial.print(F(" transfer_retries="));
  Serial.print(transfer.retries);
  Serial.print(F(" hci_initial="));
  Serial.print(capture.initialEvents);
  Serial.print(F(" hci_continue="));
  Serial.print(capture.continuationEvents);
  Serial.print(F(" rejected="));
  Serial.print(capture.rejectedEvents);
  Serial.print(F(" dropped="));
  Serial.println(capture.droppedPackets);
}

}  // namespace

void setup() {
  configureBoard();
  Serial.println(F("CoreBleChannelSoundingReflector start"));
  Serial.println(F("role=reflector sounding=bluetooth_le_cs_test cpu_mhz=128"));
  Serial.println(F("controller=nordic_sdc result_transport=crc_radio"));
  Serial.println(F("connected_acl_cs=0 qualification_claimed=0"));
}

void loop() {
  ++gCycle;
  BleCsStepTransferStats sessionTransfer{};
  if (!awaitCorrelatedSession(&sessionTransfer)) {
    Serial.print(F("cs_result role=reflector result=RETRY cycle="));
    Serial.print(gCycle);
    Serial.print(F(" reason=session_sync bytes="));
    Serial.print(sessionTransfer.bytesTransferred);
    Serial.print(F(" rejected="));
    Serial.println(sessionTransfer.rejectedFrames);
    delay(kRetryDelayMs);
    return;
  }
  if (!runControllerTest(&gCapture)) {
    printRetry(gCapture);
    delay(kRetryDelayMs);
    return;
  }

  BleCsPeerResultMetadata metadata{};
  metadata.session = gSession;
  metadata.startAclConnEventCounter =
      gCapture.header.startAclConnEventCounter;
  metadata.procedureCounter = gCapture.header.procedureCounter;
  metadata.numStepsReported = gCapture.header.numStepsReported;
  metadata.role = static_cast<uint8_t>(BleCsControllerRole::kReflector);
  metadata.configId = gCapture.header.configId;
  metadata.numAntennaPaths = gCapture.header.numAntennaPaths;
  metadata.mainModeType = gTestConfig.mainModeType;
  metadata.subModeType = gTestConfig.subModeType;
  metadata.rttType = gTestConfig.rttType;
  uint16_t envelopeLen = 0U;
  if (!BleChannelSoundingRadio::encodePeerResultEnvelope(
          metadata, gCapture.stepData, gCapture.stepDataLen, gPeerEnvelope,
          sizeof(gPeerEnvelope), &envelopeLen) ||
      !beginPeerTransport()) {
    Serial.print(F("cs_result role=reflector result=RETRY cycle="));
    Serial.print(gCycle);
    Serial.println(F(" reason=result_envelope"));
    delay(kRetryDelayMs);
    return;
  }

  BleCsStepTransferStats transfer{};
  const bool sent = gTransferRadio.sendPeerStepData(
      gPeerEnvelope, envelopeLen, transferIdForSession(gSession),
      kResultSendTimeoutMs, &transfer);
  gTransferRadio.end();
  if (!sent) {
    Serial.print(F("cs_result role=reflector result=RETRY cycle="));
    Serial.print(gCycle);
    Serial.print(F(" reason=peer_transfer bytes="));
    Serial.print(transfer.bytesTransferred);
    Serial.print(F(" retries="));
    Serial.println(transfer.retries);
    delay(kRetryDelayMs);
    return;
  }

  ++gTransferredResults;
  led(true);
  printTransfer(gCapture, transfer);
  delay(20U);
  led(false);
  delay(kRetryDelayMs);
}
