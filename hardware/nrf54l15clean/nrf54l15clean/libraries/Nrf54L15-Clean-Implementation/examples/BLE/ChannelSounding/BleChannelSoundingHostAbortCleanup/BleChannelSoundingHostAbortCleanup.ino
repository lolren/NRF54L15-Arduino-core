/*
 * BleChannelSoundingHostAbortCleanup
 *
 * Host-only Channel Sounding regression probe. It injects synthetic HCI LE CS
 * Subevent Result packets directly into BleCsControllerHost and verifies that
 * an aborted result clears any already-accumulated local/peer data. Without
 * that cleanup, a later valid packet can be combined with stale pre-abort data.
 *
 * Serial output:
 *   cs_host_abort_cleanup=PASS stale_blocked=1 recovery=1 direct_ingress=1 abort=0xB/0x0
 */

#include <Arduino.h>

#include <math.h>
#include <string.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t kConfigId = 1U;
constexpr uint8_t kAbortConnectionTerminatedByLocalHost = 0x0BU;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSpeedOfLightMetersPerSecond = 299792458.0f;
constexpr float kDemoDistanceMeters = 0.75f;
constexpr float kDemoAmplitude = 1024.0f;
const uint8_t kDemoChannels[] = {0U, 12U, 24U, 36U};

BleCsControllerHost gHost;

void writeLe16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void encodePctSampleBytes(int16_t i, int16_t q, uint8_t outPct[3]) {
  auto clamp12 = [](int16_t value) -> int16_t {
    if (value < -2048) {
      return -2048;
    }
    if (value > 2047) {
      return 2047;
    }
    return value;
  };

  const uint16_t i12 = static_cast<uint16_t>(clamp12(i)) & 0x0FFFU;
  const uint16_t q12 = static_cast<uint16_t>(clamp12(q)) & 0x0FFFU;
  const uint32_t packed =
      static_cast<uint32_t>(i12) | (static_cast<uint32_t>(q12) << 12U);
  outPct[0] = static_cast<uint8_t>(packed & 0xFFU);
  outPct[1] = static_cast<uint8_t>((packed >> 8U) & 0xFFU);
  outPct[2] = static_cast<uint8_t>((packed >> 16U) & 0xFFU);
}

bool appendMode2Step(uint8_t* buffer,
                     size_t maxLen,
                     size_t* offset,
                     uint8_t channel,
                     int16_t i,
                     int16_t q) {
  if (buffer == nullptr || offset == nullptr || (maxLen - *offset) < 8U) {
    return false;
  }

  buffer[*offset + 0U] = kBleCsMainMode2;
  buffer[*offset + 1U] = channel;
  buffer[*offset + 2U] = 5U;
  buffer[*offset + 3U] = 0U;
  encodePctSampleBytes(i, q, &buffer[*offset + 4U]);
  buffer[*offset + 7U] = kBleCsToneQualityHigh;
  *offset += 8U;
  return true;
}

uint8_t csFrequencyOffsetMHz(uint8_t channel) {
  return (channel <= 10U) ? static_cast<uint8_t>(4U + (2U * channel))
                          : static_cast<uint8_t>(6U + (2U * channel));
}

bool buildSteps(uint8_t* localSteps,
                size_t localMaxLen,
                size_t* localLen,
                uint8_t* peerSteps,
                size_t peerMaxLen,
                size_t* peerLen) {
  if (localSteps == nullptr || localLen == nullptr || peerSteps == nullptr ||
      peerLen == nullptr) {
    return false;
  }

  *localLen = 0U;
  *peerLen = 0U;
  for (size_t i = 0U; i < sizeof(kDemoChannels); ++i) {
    const uint8_t channel = kDemoChannels[i];
    const float freqHz =
        (2400.0f + static_cast<float>(csFrequencyOffsetMHz(channel))) *
        1000000.0f;
    const float theta = -((4.0f * kPi * kDemoDistanceMeters * freqHz) /
                          kSpeedOfLightMetersPerSecond);
    const int16_t peerI =
        static_cast<int16_t>(lroundf(cosf(theta) * kDemoAmplitude));
    const int16_t peerQ =
        static_cast<int16_t>(lroundf(sinf(theta) * kDemoAmplitude));
    if (!appendMode2Step(localSteps, localMaxLen, localLen, channel, 1024, 0) ||
        !appendMode2Step(peerSteps, peerMaxLen, peerLen, channel, peerI, peerQ)) {
      return false;
    }
  }
  return true;
}

bool buildH4LeMetaEvent(uint8_t* out,
                        size_t maxLen,
                        uint8_t subeventCode,
                        const uint8_t* payload,
                        size_t payloadLen,
                        size_t* outLen) {
  if (out == nullptr || payload == nullptr || outLen == nullptr ||
      maxLen < (4U + payloadLen) || payloadLen > 254U) {
    return false;
  }

  out[0U] = kBleHciPacketTypeEvent;
  out[1U] = kBleHciEvtLeMeta;
  out[2U] = static_cast<uint8_t>(1U + payloadLen);
  out[3U] = subeventCode;
  memcpy(out + 4U, payload, payloadLen);
  *outLen = 4U + payloadLen;
  return true;
}

bool buildInitialResultPacket(uint8_t* out,
                              size_t maxLen,
                              uint16_t procedureCounter,
                              const uint8_t* steps,
                              size_t stepLen,
                              uint8_t procedureDoneStatus,
                              uint8_t subeventDoneStatus,
                              uint8_t procedureAbortReason,
                              uint8_t subeventAbortReason,
                              size_t* outLen) {
  if (out == nullptr || steps == nullptr || outLen == nullptr ||
      stepLen > 200U) {
    return false;
  }

  uint8_t payload[96] = {0};
  if ((15U + stepLen) > sizeof(payload)) {
    return false;
  }

  writeLe16(payload + 0U, kConnHandle);
  payload[2U] = kConfigId;
  writeLe16(payload + 3U, 0x1234U);
  writeLe16(payload + 5U, procedureCounter);
  writeLe16(payload + 7U, 0U);
  payload[9U] = 0U;
  payload[10U] = procedureDoneStatus;
  payload[11U] = subeventDoneStatus;
  payload[12U] = static_cast<uint8_t>((procedureAbortReason & 0x0FU) |
                                      ((subeventAbortReason & 0x0FU) << 4U));
  payload[13U] = 2U;
  payload[14U] = static_cast<uint8_t>(stepLen / 8U);
  if (stepLen > 0U) {
    memcpy(payload + 15U, steps, stepLen);
  }

  return buildH4LeMetaEvent(out, maxLen, kBleCsHciEvtSubeventResult, payload,
                            15U + stepLen, outLen);
}

bool sendResult(BleCsControllerIngressSource source,
                const uint8_t* packet,
                size_t packetLen,
                bool expectedOk) {
  const bool consumed = gHost.consumeIngressPacket(source, packet, packetLen);
  return consumed == expectedOk;
}

bool dummySendPacket(const uint8_t*, size_t, void*) {
  return true;
}

bool beginHost() {
  BleCsControllerVprHostConfig vprConfig{};
  BleCsControllerVprHost::fillDemoConfig(&vprConfig);

  BleCsControllerHostConfig hostConfig{};
  hostConfig.session = vprConfig.session;
  hostConfig.sendPacket = dummySendPacket;
  hostConfig.userData = nullptr;
  return gHost.begin(kConnHandle, hostConfig);
}

bool buildCompletedResult(uint16_t procedureCounter,
                          const uint8_t* steps,
                          size_t stepLen,
                          BleCsSubeventResult* outResult) {
  if (steps == nullptr || outResult == nullptr || stepLen == 0U ||
      stepLen > 0xFFFFU) {
    return false;
  }

  BleCsSubeventResult result{};
  result.header.connHandle = kConnHandle;
  result.header.configId = kConfigId;
  result.header.procedureCounter = procedureCounter;
  result.header.procedureDoneStatus = kBleCsProcedureDoneComplete;
  result.header.subeventDoneStatus = kBleCsSubeventDoneComplete;
  result.header.numAntennaPaths = 2U;
  result.header.numStepsReported = static_cast<uint16_t>(stepLen / 8U);
  result.stepData = steps;
  result.stepDataLen = static_cast<uint16_t>(stepLen);
  result.isComplete = true;
  *outResult = result;
  return true;
}

bool runDirectResultIngressProbe(const uint8_t* localSteps,
                                 size_t localStepsLen,
                                 const uint8_t* peerSteps,
                                 size_t peerStepsLen) {
  if (!beginHost()) {
    return false;
  }

  BleCsSubeventResult localResult{};
  BleCsSubeventResult peerResult{};
  const bool built =
      buildCompletedResult(3U, localSteps, localStepsLen, &localResult) &&
      buildCompletedResult(3U, peerSteps, peerStepsLen, &peerResult);
  const bool localOk =
      built && gHost.consumeCompletedResult(BleCsControllerResultSource::kLocal,
                                            localResult);
  const bool peerOk =
      localOk && gHost.consumeCompletedResult(BleCsControllerResultSource::kPeer,
                                              peerResult);
  return peerOk && gHost.estimateValid() &&
         gHost.sessionState().completedProcedureCounter == 3U &&
         gHost.state().localSubeventResults >= 1U &&
         gHost.state().peerSubeventResults >= 1U;
}

bool runAbortCleanupProbe() {
  uint8_t localSteps[64] = {0};
  uint8_t peerSteps[64] = {0};
  size_t localStepsLen = 0U;
  size_t peerStepsLen = 0U;
  bool ok = beginHost() &&
            buildSteps(localSteps, sizeof(localSteps), &localStepsLen,
                       peerSteps, sizeof(peerSteps), &peerStepsLen);

  uint8_t localPacket[128] = {0};
  uint8_t peerPacket[128] = {0};
  uint8_t abortPacket[64] = {0};
  size_t localPacketLen = 0U;
  size_t peerPacketLen = 0U;
  size_t abortPacketLen = 0U;

  ok = ok &&
       buildInitialResultPacket(localPacket, sizeof(localPacket), 1U, localSteps,
                                localStepsLen, kBleCsProcedureDoneComplete,
                                kBleCsSubeventDoneComplete, 0U, 0U,
                                &localPacketLen) &&
       buildInitialResultPacket(abortPacket, sizeof(abortPacket), 1U, peerSteps,
                                0U, kBleCsProcedureDoneAborted,
                                kBleCsSubeventDoneAborted,
                                kAbortConnectionTerminatedByLocalHost, 0U,
                                &abortPacketLen) &&
       buildInitialResultPacket(peerPacket, sizeof(peerPacket), 1U, peerSteps,
                                peerStepsLen, kBleCsProcedureDoneComplete,
                                kBleCsSubeventDoneComplete, 0U, 0U,
                                &peerPacketLen);

  ok = ok && sendResult(BleCsControllerIngressSource::kLocalResult, localPacket,
                        localPacketLen, true);
  const bool abortRejected =
      ok && sendResult(BleCsControllerIngressSource::kPeerResult, abortPacket,
                       abortPacketLen, false);
  const uint8_t procedureAbort = gHost.lastProcedureAbortReason();
  const uint8_t subeventAbort = gHost.lastSubeventAbortReason();

  ok = ok && abortRejected &&
       procedureAbort == kAbortConnectionTerminatedByLocalHost &&
       subeventAbort == 0U && !gHost.estimateValid();

  /* If the aborted peer result did not clear the pre-abort local result, this
   * valid peer packet would complete procedure 1 using stale local data. */
  ok = ok && sendResult(BleCsControllerIngressSource::kPeerResult, peerPacket,
                        peerPacketLen, true);
  const bool staleBlocked = !gHost.estimateValid() &&
                            gHost.sessionState().completedProcedureCounter == 0U;

  ok = ok &&
       buildInitialResultPacket(localPacket, sizeof(localPacket), 2U, localSteps,
                                localStepsLen, kBleCsProcedureDoneComplete,
                                kBleCsSubeventDoneComplete, 0U, 0U,
                                &localPacketLen) &&
       buildInitialResultPacket(peerPacket, sizeof(peerPacket), 2U, peerSteps,
                                peerStepsLen, kBleCsProcedureDoneComplete,
                                kBleCsSubeventDoneComplete, 0U, 0U,
                                &peerPacketLen) &&
       sendResult(BleCsControllerIngressSource::kLocalResult, localPacket,
                  localPacketLen, true) &&
       sendResult(BleCsControllerIngressSource::kPeerResult, peerPacket,
                  peerPacketLen, true);
  const bool recovery = gHost.estimateValid() &&
                        gHost.sessionState().completedProcedureCounter == 2U;
  const bool directIngress =
      ok && runDirectResultIngressProbe(localSteps, localStepsLen,
                                        peerSteps, peerStepsLen);

  Serial.print(F("cs_host_abort_cleanup="));
  Serial.print((ok && staleBlocked && recovery && directIngress) ? F("PASS") : F("FAIL"));
  Serial.print(F(" stale_blocked="));
  Serial.print(staleBlocked ? 1 : 0);
  Serial.print(F(" recovery="));
  Serial.print(recovery ? 1 : 0);
  Serial.print(F(" direct_ingress="));
  Serial.print(directIngress ? 1 : 0);
  Serial.print(F(" abort=0x"));
  Serial.print(procedureAbort, HEX);
  Serial.print(F("/0x"));
  Serial.println(subeventAbort, HEX);
  return ok && staleBlocked && recovery && directIngress;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }
  Serial.println(F("BleChannelSoundingHostAbortCleanup"));
  (void)runAbortCleanupProbe();
}

void loop() {}
