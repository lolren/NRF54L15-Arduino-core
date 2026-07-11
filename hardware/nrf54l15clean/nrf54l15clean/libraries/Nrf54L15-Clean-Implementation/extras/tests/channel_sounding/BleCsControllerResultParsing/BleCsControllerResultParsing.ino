#include <Arduino.h>

#include <math.h>
#include <string.h>

#include "ble_channel_sounding.h"

using namespace xiao_nrf54l15;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kSpeedOfLight = 299792458.0f;

void encodePct(int16_t i, int16_t q, uint8_t* out) {
  const uint32_t packed =
      (static_cast<uint16_t>(i) & 0x0FFFU) |
      ((static_cast<uint32_t>(static_cast<uint16_t>(q) & 0x0FFFU)) << 12U);
  out[0] = static_cast<uint8_t>(packed & 0xFFU);
  out[1] = static_cast<uint8_t>((packed >> 8U) & 0xFFU);
  out[2] = static_cast<uint8_t>((packed >> 16U) & 0xFFU);
}

void appendMode2(uint8_t* buffer, size_t* offset, uint8_t channel,
                 int16_t i, int16_t q) {
  buffer[*offset + 0U] = kBleCsMainMode2;
  buffer[*offset + 1U] = channel;
  buffer[*offset + 2U] = 5U;
  buffer[*offset + 3U] = 0U;
  encodePct(i, q, &buffer[*offset + 4U]);
  buffer[*offset + 7U] = kBleCsToneQualityHigh;
  *offset += 8U;
}

void appendMode1(uint8_t* buffer, size_t* offset, uint8_t channel,
                 int16_t timeDifferenceHalfNs) {
  buffer[*offset + 0U] = kBleCsMainMode1;
  buffer[*offset + 1U] = channel;
  buffer[*offset + 2U] = 6U;
  buffer[*offset + 3U] = kBleCsPacketQualityAaCheckOk;
  buffer[*offset + 4U] = 0U;
  buffer[*offset + 5U] = static_cast<uint8_t>(-40);
  buffer[*offset + 6U] =
      static_cast<uint8_t>(timeDifferenceHalfNs & 0xFF);
  buffer[*offset + 7U] = static_cast<uint8_t>(
      (static_cast<uint16_t>(timeDifferenceHalfNs) >> 8U) & 0xFFU);
  buffer[*offset + 8U] = 1U;
  *offset += 9U;
}

bool checkControllerChannelMapping() {
  constexpr uint8_t channels[] = {2U, 22U, 26U, 40U, 58U, 76U};
  constexpr float expectedMeters = 1.0f;
  uint8_t local[sizeof(channels) * 8U] = {0};
  uint8_t peer[sizeof(channels) * 8U] = {0};
  size_t localLen = 0U;
  size_t peerLen = 0U;
  const float referenceHz = (2402.0f + channels[0]) * 1000000.0f;
  for (uint8_t channel : channels) {
    const float frequencyHz = (2402.0f + channel) * 1000000.0f;
    const float phase = -4.0f * kPi * expectedMeters *
                        (frequencyHz - referenceHz) / kSpeedOfLight;
    appendMode2(local, &localLen, channel, 1600, 0);
    appendMode2(peer, &peerLen, channel,
                static_cast<int16_t>(lroundf(cosf(phase) * 1600.0f)),
                static_cast<int16_t>(lroundf(sinf(phase) * 1600.0f)));
  }

  BleCsEstimate estimate{};
  return BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
             local, localLen, peer, peerLen, true, &estimate) &&
         estimate.valid && estimate.usedChannels == sizeof(channels) &&
         estimate.distanceMeters > 0.95f && estimate.distanceMeters < 1.05f;
}

bool checkMalformedBuffersRejected() {
  constexpr uint8_t channels[] = {2U, 22U, 26U, 40U, 58U, 76U};
  constexpr float expectedMeters = 1.0f;
  uint8_t local[sizeof(channels) * 8U] = {0};
  uint8_t peer[(sizeof(channels) * 8U) + 2U] = {0};
  size_t localLen = 0U;
  size_t peerLen = 0U;
  const float referenceHz = (2402.0f + channels[0]) * 1000000.0f;
  for (uint8_t channel : channels) {
    const float frequencyHz = (2402.0f + channel) * 1000000.0f;
    const float phase = -4.0f * kPi * expectedMeters *
                        (frequencyHz - referenceHz) / kSpeedOfLight;
    appendMode2(local, &localLen, channel, 1600, 0);
    appendMode2(peer, &peerLen, channel,
                static_cast<int16_t>(lroundf(cosf(phase) * 1600.0f)),
                static_cast<int16_t>(lroundf(sinf(phase) * 1600.0f)));
  }

  BleCsEstimate estimate{};
  if (!BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
          local, localLen, peer, peerLen, true, &estimate)) {
    return false;
  }
  if (BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
          local, localLen, peer, peerLen - 8U, true, &estimate)) {
    return false;
  }

  peer[peerLen++] = kBleCsMainMode2;
  peer[peerLen++] = 44U;
  if (BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
          local, localLen, peer, peerLen, true, &estimate)) {
    return false;
  }

  uint8_t reordered[sizeof(channels) * 8U] = {0};
  memcpy(reordered, peer + 8U, 8U);
  memcpy(reordered + 8U, peer, 8U);
  memcpy(reordered + 16U, peer + 16U, sizeof(reordered) - 16U);
  if (BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
          local, localLen, reordered, sizeof(reordered), true, &estimate)) {
    return false;
  }

  uint8_t duplicateLocal[(sizeof(channels) + 1U) * 8U] = {0};
  uint8_t duplicatePeer[(sizeof(channels) + 1U) * 8U] = {0};
  memcpy(duplicateLocal, local, localLen);
  memcpy(duplicatePeer, peer, localLen);
  size_t duplicateLocalLen = localLen;
  size_t duplicatePeerLen = localLen;
  appendMode2(duplicateLocal, &duplicateLocalLen, channels[0], 1600, 0);
  appendMode2(duplicatePeer, &duplicatePeerLen, channels[0], 1600, 0);
  if (BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
          duplicateLocal, duplicateLocalLen, duplicatePeer, duplicatePeerLen,
          true, &estimate)) {
    return false;
  }

  BleCsSubeventStep zeroTone{};
  const uint8_t zeroToneData[] = {0U};
  zeroTone.mode = kBleCsMainMode2;
  zeroTone.channel = 40U;
  zeroTone.dataLen = sizeof(zeroToneData);
  zeroTone.data = zeroToneData;
  BleCsStepMode2Data parsedZeroTone{};
  return !BleChannelSoundingRadio::parseMode2StepData(&zeroTone,
                                                      &parsedZeroTone);
}

bool checkIncoherentPhaseRejected() {
  constexpr uint8_t channels[] = {40U, 46U, 52U, 58U, 64U, 70U, 76U};
  constexpr int16_t peerI[] = {1600, 250, 580, -1450, -1370, 1400, 1110};
  constexpr int16_t peerQ[] = {0, 1580, -1490, 680, -830, 770, -1150};
  uint8_t local[sizeof(channels) * 8U] = {0};
  uint8_t peer[sizeof(channels) * 8U] = {0};
  size_t localLen = 0U;
  size_t peerLen = 0U;
  for (size_t i = 0U; i < sizeof(channels); ++i) {
    appendMode2(local, &localLen, channels[i], 1600, 0);
    appendMode2(peer, &peerLen, channels[i], peerI[i], peerQ[i]);
  }

  BleCsEstimate estimate{};
  return !BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
      local, localLen, peer, peerLen, true, &estimate);
}

bool checkSingleRttPairRejected() {
  uint8_t local[9] = {0};
  uint8_t peer[9] = {0};
  size_t localLen = 0U;
  size_t peerLen = 0U;
  appendMode1(local, &localLen, 40U, 100);
  appendMode1(peer, &peerLen, 40U, 20);
  BleCsEstimate estimate{};
  return !BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
      local, localLen, peer, peerLen, true, &estimate);
}

bool checkPeerCorrelationProtocol() {
  const BleCsPeerSession session{
      .token = 0x12345678UL,
      .profileTag = 0x43534D32UL,
      .drbgNonce = 0x4321U,
  };
  uint8_t request[kBleCsPeerSessionRequestBytes] = {0};
  uint16_t requestLen = 0U;
  BleCsPeerSession decodedSession{};
  if (!BleChannelSoundingRadio::encodePeerSessionRequest(
          session, request, sizeof(request), &requestLen) ||
      requestLen != sizeof(request) ||
      !BleChannelSoundingRadio::decodePeerSessionRequest(
          request, requestLen, &decodedSession) ||
      decodedSession.token != session.token ||
      decodedSession.profileTag != session.profileTag ||
      decodedSession.drbgNonce != session.drbgNonce ||
      BleChannelSoundingRadio::decodePeerSessionRequest(
          request, requestLen - 1U, &decodedSession)) {
    return false;
  }

  uint8_t stepData[8] = {0};
  size_t stepDataLen = 0U;
  appendMode2(stepData, &stepDataLen, 40U, 1000, 0);
  BleCsPeerResultMetadata metadata{};
  metadata.session = session;
  metadata.startAclConnEventCounter = 7U;
  metadata.procedureCounter = 9U;
  metadata.numStepsReported = 1U;
  metadata.role = 1U;
  metadata.configId = 3U;
  metadata.numAntennaPaths = 1U;
  metadata.mainModeType = 2U;
  metadata.subModeType = 1U;
  metadata.rttType = 0U;
  uint8_t envelope[64] = {0};
  uint16_t envelopeLen = 0U;
  BleCsPeerResultMetadata decodedMetadata{};
  const uint8_t* decodedStepData = nullptr;
  uint16_t decodedStepDataLen = 0U;
  return BleChannelSoundingRadio::encodePeerResultEnvelope(
             metadata, stepData, stepDataLen, envelope, sizeof(envelope),
             &envelopeLen) &&
         BleChannelSoundingRadio::decodePeerResultEnvelope(
             envelope, envelopeLen, &decodedMetadata, &decodedStepData,
             &decodedStepDataLen) &&
         decodedMetadata.session.token == session.token &&
         decodedMetadata.session.profileTag == session.profileTag &&
         decodedMetadata.procedureCounter == metadata.procedureCounter &&
         decodedMetadata.role == metadata.role &&
         decodedStepDataLen == stepDataLen &&
         memcmp(decodedStepData, stepData, stepDataLen) == 0 &&
         !BleChannelSoundingRadio::decodePeerResultEnvelope(
             envelope, envelopeLen - 1U, &decodedMetadata, &decodedStepData,
             &decodedStepDataLen);
}

bool checkRawToControllerEncoding() {
  constexpr uint8_t rawChannels[] = {0U, 10U, 11U, 36U};
  constexpr uint8_t expectedCsChannels[] = {2U, 22U, 26U, 76U};
  BleCsChannelMeasurement measurements[sizeof(rawChannels)] = {};
  for (size_t i = 0U; i < sizeof(rawChannels); ++i) {
    measurements[i].valid = true;
    measurements[i].channelIndex = rawChannels[i];
    measurements[i].localTone.valid = true;
    measurements[i].localTone.i = 1000;
    measurements[i].localTone.magnitude = 1000U;
  }

  uint8_t encoded[64] = {0};
  size_t encodedLen = 0U;
  uint16_t encodedSteps = 0U;
  if (!BleChannelSoundingRadio::encodeMode2StepDataFromMeasurements(
          measurements, sizeof(rawChannels), false, encoded, sizeof(encoded),
          &encodedLen, &encodedSteps) ||
      encodedLen != sizeof(rawChannels) * 8U ||
      encodedSteps != sizeof(rawChannels)) {
    return false;
  }
  for (size_t i = 0U; i < sizeof(expectedCsChannels); ++i) {
    if (encoded[(i * 8U) + 1U] != expectedCsChannels[i]) {
      return false;
    }
  }
  return true;
}

bool checkMode3Layouts() {
  uint8_t normalData[15] = {0};
  normalData[6] = 0U;
  encodePct(1000, 0, &normalData[7]);
  normalData[10] = kBleCsToneQualityHigh;
  encodePct(0, 1000, &normalData[11]);
  normalData[14] = kBleCsToneQualityHigh;
  BleCsSubeventStep normal{};
  normal.mode = kBleCsMainMode3;
  normal.channel = 40U;
  normal.dataLen = sizeof(normalData);
  normal.data = normalData;

  BleCsStepMode3Data parsedNormal{};
  BleCsStepToneInfo normalTone{};
  if (!BleChannelSoundingRadio::parseMode3StepData(&normal, &parsedNormal) ||
      parsedNormal.timing.hasRttSoundingSequence ||
      parsedNormal.toneDataOffset != 7U || parsedNormal.toneCount != 2U ||
      !BleChannelSoundingRadio::parseMode3ToneInfo(&normal, 1U, &normalTone) ||
      normalTone.pct.i != 0 || normalTone.pct.q != 1000 ||
      BleChannelSoundingRadio::parseMode3StepData(&normal, true,
                                                  &parsedNormal)) {
    return false;
  }

  uint8_t ssData[23] = {0};
  encodePct(400, -300, &ssData[6]);
  encodePct(-200, 500, &ssData[10]);
  ssData[14] = 0U;
  encodePct(900, 100, &ssData[15]);
  ssData[18] = kBleCsToneQualityHigh;
  encodePct(100, 900, &ssData[19]);
  ssData[22] = kBleCsToneQualityHigh;
  BleCsSubeventStep sounding{};
  sounding.mode = kBleCsMainMode3;
  sounding.channel = 58U;
  sounding.dataLen = sizeof(ssData);
  sounding.data = ssData;
  BleCsStepMode3Data parsedSounding{};
  return BleChannelSoundingRadio::parseMode3StepData(
             &sounding, true, &parsedSounding) &&
         parsedSounding.timing.hasRttSoundingSequence &&
         parsedSounding.toneDataOffset == 15U &&
         parsedSounding.toneCount == 2U &&
         parsedSounding.timing.soundingPct1.i == 400 &&
         parsedSounding.timing.soundingPct1.q == -300;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300U);
  const bool channelMapping = checkControllerChannelMapping();
  const bool malformedRejected = checkMalformedBuffersRejected();
  const bool incoherentRejected = checkIncoherentPhaseRejected();
  const bool singleRttRejected = checkSingleRttPairRejected();
  const bool correlationProtocol = checkPeerCorrelationProtocol();
  const bool encoding = checkRawToControllerEncoding();
  const bool mode3 = checkMode3Layouts();
  Serial.print(F("cs_controller_result_parsing="));
  Serial.print(channelMapping && malformedRejected && incoherentRejected &&
                       singleRttRejected && correlationProtocol && encoding &&
                       mode3
                   ? F("PASS")
                   : F("FAIL"));
  Serial.print(F(" channel_mapping="));
  Serial.print(channelMapping ? 1 : 0);
  Serial.print(F(" malformed_rejected="));
  Serial.print(malformedRejected ? 1 : 0);
  Serial.print(F(" raw_encoding="));
  Serial.print(encoding ? 1 : 0);
  Serial.print(F(" incoherent_rejected="));
  Serial.print(incoherentRejected ? 1 : 0);
  Serial.print(F(" single_rtt_rejected="));
  Serial.print(singleRttRejected ? 1 : 0);
  Serial.print(F(" correlation_protocol="));
  Serial.print(correlationProtocol ? 1 : 0);
  Serial.print(F(" mode3_layouts="));
  Serial.println(mode3 ? 1 : 0);
}

void loop() {
  delay(1000U);
}
