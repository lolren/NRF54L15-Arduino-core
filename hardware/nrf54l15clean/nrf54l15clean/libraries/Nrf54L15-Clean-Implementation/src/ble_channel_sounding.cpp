#include "ble_channel_sounding.h"

#include <Arduino.h>

#include <math.h>
#include <string.h>

namespace xiao_nrf54l15 {

namespace {

constexpr uint8_t kMagic0 = 'C';
constexpr uint8_t kMagic1 = 'S';
constexpr uint8_t kPayloadHeaderLen = 6U;
constexpr uint8_t kReportToneExtraLen = 11U;
constexpr uint8_t kReportRttExtraLen = 9U;
constexpr uint8_t kReportExtraLen = kReportToneExtraLen + kReportRttExtraLen;
constexpr uint8_t kStepTransferVersion = 1U;
constexpr uint8_t kStepTransferHeaderLen = 11U;
constexpr uint8_t kStepAckLen = 6U;
constexpr uint8_t kStepTransferChannel = 18U;
constexpr uint8_t kStepTransferChunkLimit = 220U;
constexpr uint32_t kStepTransferListenWindowUs = 30000U;
constexpr uint16_t kStepTransferAckTurnaroundUs = 1200U;
constexpr uint32_t kStepTransferFinalAckLingerMs = 180U;
constexpr uint8_t kPeerProtocolVersion = 1U;
constexpr uint32_t kPeerSessionMagic = 0x31515343UL;  // "CSQ1"
constexpr uint32_t kPeerResultMagic = 0x31525343UL;   // "CSR1"
constexpr uint8_t kCteTypeAoA = 0U;
constexpr uint32_t kBleCrcPolynomial = 0x00065BUL;
constexpr uint32_t kCstStartBudgetUs = 1500U;
constexpr uint32_t kRadioEndBudgetUs = 3000U;
constexpr uint32_t kRadioDisableBudgetUs = 3000U;
constexpr uint32_t kAuxDataBudgetUs = 3000U;
constexpr uint32_t kSpinLimit = 3000000UL;
constexpr uint8_t kMicrosPollDivider = 32U;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSpeedOfLightMetersPerSecond = 299792458.0f;
constexpr float kAdjacentChannelSpacingHz = 2000000.0f;
constexpr float kAdjacentChannelSpacingToleranceHz = 1024.0f;
constexpr float kMinimumAdjacentPhaseCoherence = 0.50f;
constexpr uint8_t kMinimumAdjacentPhasePairs = 4U;
constexpr size_t kMaxCsChannels = 37U;
constexpr size_t kMaxSlopePairs =
    (kMaxCsChannels * (kMaxCsChannels - 1U)) / 2U;
constexpr size_t kMaxCsControllerStepSamples = 256U;
constexpr size_t kMinimumControllerPbrChannels = 4U;
constexpr size_t kMinimumControllerRttPairs = 3U;
constexpr float kMinimumControllerPbrSpanHz = 10000000.0f;
constexpr float kMaximumControllerPbrResidualVariance = 0.50f;

bool channelSoundingHfxoRunning() {
  return ((NRF_CLOCK->XO.STAT & CLOCK_XO_STAT_STATE_Msk) >>
          CLOCK_XO_STAT_STATE_Pos) == CLOCK_XO_STAT_STATE_Running;
}

bool channelSoundingRadioDisabled(const NRF_RADIO_Type* radio) {
  return radio == nullptr ||
         (((radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos) ==
          RADIO_STATE_STATE_Disabled);
}

bool releaseRawCsRadioOwnershipIfDisabled(uint32_t* token,
                                          NRF_RADIO_Type* radio) {
  if (token == nullptr || *token == 0U) {
    return true;
  }
  if (!channelSoundingRadioDisabled(radio) ||
      !nrf54l15_scrub_owned_radio_dma(
          Nrf54ExclusiveRadioOwner::kRawChannelSounding, *token, radio) ||
      !nrf54l15_release_exclusive_radio(
          Nrf54ExclusiveRadioOwner::kRawChannelSounding, *token)) {
    return false;
  }
  *token = 0U;
  return true;
}
constexpr uint8_t kCsChmapLen = 10U;
constexpr uint8_t kAntennaId1 = 0x1U;
constexpr uint8_t kAntennaId2 = 0x2U;
constexpr uint8_t kAntennaId3 = 0x3U;
constexpr uint8_t kAntennaId4 = 0x4U;
constexpr size_t kBleCsStepHeaderLen = 3U;
constexpr size_t kBleCsMode1StepLen = 6U;
constexpr size_t kBleCsMode1SsRttStepLen = 14U;
constexpr size_t kBleCsToneInfoLen = 4U;
constexpr size_t kBleCsMode3StepBaseLen = 7U;
constexpr size_t kBleCsMode3SsRttStepBaseLen = 15U;
constexpr size_t kBleCsHciSubeventResultHeaderLen = 15U;
constexpr size_t kBleCsHciSubeventResultContinueHeaderLen = 8U;
constexpr size_t kBleCsHciLeMetaMaxPayloadLen = 254U;
/* Reserved connection handle for standalone CS Test subevent results
 * (Zephyr BT_HCI_LE_CS_TEST_CONN_HANDLE). */
constexpr uint16_t kBleCsHciTestConnHandle = 0x0FFFU;
constexpr size_t kBleCsHciReadRemoteCapsCompleteLen = 31U;
constexpr size_t kBleCsHciReadRemoteCapsCompleteV2Len = 34U;
constexpr size_t kBleCsHciSecurityEnableCompleteLen = 3U;
constexpr size_t kBleCsHciConfigCompleteLen = 33U;
constexpr size_t kBleCsHciProcedureEnableCompleteLen = 21U;
constexpr uint8_t kBleCsVprVendorEvtPeerResultTrigger = 0xB1U;
constexpr uint8_t kBleCsVprVendorEvtPeerResultSource = 0xB2U;
constexpr uint8_t kBleCsVprPacketMaxPayloadDefault = 32U;
constexpr uint32_t kBleCsVprPacketPcnf0Default = 0x01080108UL;
constexpr uint32_t kBleCsVprPacketPcnf1Default = 0x02030020UL;
constexpr uint8_t kBleCsVprPacketTypeProbe = 0x50U;
struct BleCsControllerPhasePair {
  bool failed = false;
  bool localPresent = false;
  bool peerPresent = false;
  uint8_t channel = 0U;
  BleCsIqSample local{};
  BleCsIqSample peer{};
  uint8_t localQuality = kBleCsToneQualityUnavailable;
  uint8_t peerQuality = kBleCsToneQualityUnavailable;
};

struct BleCsControllerRttPair {
  bool failed = false;
  bool initiatorPresent = false;
  bool reflectorPresent = false;
  uint8_t channel = 0U;
  int16_t toaTodInitiator = kBleCsTimeDifferenceNotAvailable;
  int16_t todToaReflector = kBleCsTimeDifferenceNotAvailable;
};

struct BleCsControllerBufferParseContext {
  BleCsControllerPhasePair* phasePairs = nullptr;
  size_t phaseCapacity = 0U;
  size_t* phaseCount = nullptr;
  size_t phaseCursor = 0U;
  BleCsControllerRttPair* rttPairs = nullptr;
  size_t rttCapacity = 0U;
  size_t* rttCount = nullptr;
  size_t rttCursor = 0U;
  bool fillingPeer = false;
  bool bufferRoleIsInitiator = false;
};

inline uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8U);
}

inline uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) |
         (static_cast<uint32_t>(data[3]) << 24U);
}

inline void writeLe16(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

inline uint32_t readLe24(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U);
}

inline void writeLe24(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  data[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
}

inline void writeLe32(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  data[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  data[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

uint32_t fnv1a32Bytes(const uint8_t* data, size_t len) {
  uint32_t hash = 2166136261UL;
  if (data == nullptr) {
    return hash;
  }
  for (size_t i = 0U; i < len; ++i) {
    hash ^= static_cast<uint32_t>(data[i]);
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t crc32Bytes(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  if (data == nullptr && len != 0U) {
    return 0U;
  }
  for (size_t i = 0U; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      const uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

uint32_t buildMeasurementExecuteToken(uint8_t configId,
                                      uint16_t procedureCounter,
                                      uint16_t connHandle,
                                      uint8_t activeSubevent,
                                      uint8_t subeventCount,
                                      uint8_t totalSteps,
                                      uint8_t subeventStart,
                                      uint8_t subeventSteps,
                                      uint8_t stepChannelCount,
                                      const uint8_t* stepChannels,
                                      uint32_t executeCount) {
  uint8_t summary[24] = {0};
  summary[0] = 0xC5U;
  summary[1] = configId;
  writeLe16(&summary[2], procedureCounter);
  writeLe16(&summary[4], connHandle);
  summary[6] = activeSubevent;
  summary[7] = subeventCount;
  summary[8] = totalSteps;
  summary[9] = subeventStart;
  summary[10] = subeventSteps;
  summary[11] = stepChannelCount;
  for (uint8_t i = 0U; i < 6U; ++i) {
    summary[12U + i] = (stepChannels != nullptr) ? stepChannels[i] : 0U;
  }
  writeLe32(&summary[18], executeCount);
  return fnv1a32Bytes(summary, sizeof(summary));
}

uint32_t buildMeasurementRfDescriptorToken(uint8_t configId,
                                           uint16_t procedureCounter,
                                           uint16_t connHandle,
                                           uint8_t activeSubevent,
                                           uint8_t subeventCount,
                                           uint8_t totalSteps,
                                           uint8_t subeventStart,
                                           uint8_t subeventSteps,
                                           uint8_t stepChannelCount,
                                           const uint8_t* stepChannels,
                                           uint8_t role,
                                           uint8_t phy,
                                           int8_t txPowerDelta,
                                           uint8_t rttType,
                                           uint32_t minSubeventLen,
                                           uint32_t maxSubeventLen,
                                           uint32_t executeCount) {
  uint8_t summary[36] = {0};
  summary[0] = 0xC6U;
  summary[1] = configId;
  writeLe16(&summary[2], procedureCounter);
  writeLe16(&summary[4], connHandle);
  summary[6] = activeSubevent;
  summary[7] = subeventCount;
  summary[8] = totalSteps;
  summary[9] = subeventStart;
  summary[10] = subeventSteps;
  summary[11] = stepChannelCount;
  for (uint8_t i = 0U; i < 6U; ++i) {
    summary[12U + i] = (stepChannels != nullptr) ? stepChannels[i] : 0U;
  }
  summary[18] = role;
  summary[19] = phy;
  summary[20] = static_cast<uint8_t>(txPowerDelta);
  summary[21] = rttType;
  writeLe32(&summary[22], minSubeventLen);
  writeLe32(&summary[26], maxSubeventLen);
  writeLe32(&summary[30], executeCount);
  return fnv1a32Bytes(summary, sizeof(summary));
}

uint32_t buildMeasurementRfHardwareToken(uint8_t version,
                                         uint8_t flags,
                                         uint32_t radioState,
                                         uint32_t radioMode,
                                         uint32_t radioFrequency) {
  return 0xC7000000UL ^
         radioState ^
         (radioMode << 1U) ^
         (radioFrequency << 2U) ^
         (static_cast<uint32_t>(flags) << 16U) ^
         static_cast<uint32_t>(version);
}

uint32_t buildMeasurementRfPrimitiveToken(uint8_t version,
                                          uint8_t flags,
                                          uint8_t status,
                                          uint32_t stateBefore,
                                          uint32_t pllWaitLoops,
                                          uint32_t disableWaitLoops,
                                          uint32_t stateAfter) {
  return 0xC8000000UL ^
         stateBefore ^
         (pllWaitLoops << 1U) ^
         (disableWaitLoops << 2U) ^
         (stateAfter << 3U) ^
         (static_cast<uint32_t>(flags) << 16U) ^
         (static_cast<uint32_t>(status) << 24U) ^
         static_cast<uint32_t>(version);
}

uint32_t buildMeasurementRfRetuneToken(uint8_t version,
                                       uint8_t flags,
                                       uint8_t status,
                                       uint8_t channel,
                                       uint32_t targetFrequency,
                                       uint32_t targetDatawhite,
                                       uint32_t observedFrequency,
                                       uint32_t observedDatawhite) {
  return 0xCA000000UL ^
         targetFrequency ^
         (observedFrequency << 1U) ^
         targetDatawhite ^
         (observedDatawhite << 2U) ^
         (static_cast<uint32_t>(channel) << 8U) ^
         (static_cast<uint32_t>(flags) << 16U) ^
         (static_cast<uint32_t>(status) << 24U) ^
         static_cast<uint32_t>(version);
}

uint32_t buildMeasurementToneSnapshotToken(uint8_t version,
                                           uint8_t flags,
                                           uint8_t status,
                                           uint32_t pct16,
                                           uint32_t magPhase,
                                           uint32_t magStd,
                                           uint32_t frequency,
                                           uint32_t state,
                                           uint32_t cstonesEndEvent) {
  return 0xC9000000UL ^
         pct16 ^
         (magPhase << 1U) ^
         (magStd << 2U) ^
         (frequency << 3U) ^
         state ^
         (cstonesEndEvent << 4U) ^
         (static_cast<uint32_t>(flags) << 16U) ^
         (static_cast<uint32_t>(status) << 24U) ^
         static_cast<uint32_t>(version);
}

uint32_t buildMeasurementRfPacketConfigToken(uint8_t version,
                                             uint8_t flags,
                                             uint8_t status,
                                             uint32_t pcnf0,
                                             uint32_t pcnf1,
                                             uint8_t packetS0,
                                             uint8_t packetPayloadLen,
                                             uint8_t packetCteInfo,
                                             uint8_t packetMagic0,
                                             uint8_t packetMagic1,
                                             uint8_t packetType,
                                             uint8_t packetSequence,
                                             uint8_t packetChannel,
                                             uint16_t controlToProbeDelayUs,
                                             uint16_t responseListenWindowUs) {
  return pcnf0 ^ (pcnf1 << 1U) ^
         (static_cast<uint32_t>(flags) << 16U) ^
         (static_cast<uint32_t>(status) << 24U) ^
         (static_cast<uint32_t>(controlToProbeDelayUs) << 3U) ^
         (static_cast<uint32_t>(responseListenWindowUs) << 7U) ^
         (static_cast<uint32_t>(packetS0) << 2U) ^
         (static_cast<uint32_t>(packetPayloadLen) << 5U) ^
         (static_cast<uint32_t>(packetCteInfo) << 9U) ^
         (static_cast<uint32_t>(packetMagic0) << 11U) ^
         (static_cast<uint32_t>(packetMagic1) << 15U) ^
         (static_cast<uint32_t>(packetType) << 13U) ^
         (static_cast<uint32_t>(packetSequence) << 17U) ^
         (static_cast<uint32_t>(packetChannel) << 21U) ^
         static_cast<uint32_t>(version);
}

uint32_t buildMeasurementRfTimedMode2Token(uint8_t version,
                                           uint8_t flags,
                                           uint8_t status,
                                           uint8_t channel,
                                           uint8_t sequence,
                                           uint16_t controlToProbeDelayUs,
                                           uint16_t responseListenWindowUs,
                                           uint32_t txWaitLoops,
                                           uint32_t gapWaitLoops,
                                           uint32_t rxReadyWaitLoops,
                                           uint32_t listenWaitLoops,
                                           uint32_t disableWaitLoops,
                                           uint32_t stateAfter) {
  (void)disableWaitLoops;
  (void)stateAfter;
  return 0xD2000000UL ^
         static_cast<uint32_t>(version) ^
         (static_cast<uint32_t>(flags) << 16U) ^
         (static_cast<uint32_t>(status) << 24U) ^
         (static_cast<uint32_t>(channel) << 8U) ^
         (static_cast<uint32_t>(sequence) << 20U) ^
         (static_cast<uint32_t>(controlToProbeDelayUs) << 3U) ^
         (static_cast<uint32_t>(responseListenWindowUs) << 7U) ^
         txWaitLoops ^
         (gapWaitLoops << 2U) ^
         (rxReadyWaitLoops << 4U) ^
         (listenWaitLoops << 5U);
}

uint32_t buildMeasurementRfTimingOwnerToken(uint8_t version,
                                            uint8_t flags,
                                            uint8_t status,
                                            uint8_t activeSubevent,
                                            uint16_t procedureCounter,
                                            uint16_t connHandle,
                                            uint32_t heartbeat,
                                            uint32_t nextProcedureHeartbeat,
                                            uint32_t nextSubeventHeartbeat,
                                            uint32_t procedureIntervalTicks,
                                            uint32_t subeventDelayTicks,
                                            uint8_t peerGapTicks,
                                            uint8_t intervalSelector) {
  return 0xD3000000UL ^
         static_cast<uint32_t>(version) ^
         (static_cast<uint32_t>(flags) << 16U) ^
         (static_cast<uint32_t>(status) << 24U) ^
         (static_cast<uint32_t>(activeSubevent) << 8U) ^
         (static_cast<uint32_t>(procedureCounter) << 4U) ^
         (static_cast<uint32_t>(connHandle) << 5U) ^
         heartbeat ^
         (nextProcedureHeartbeat << 1U) ^
         (nextSubeventHeartbeat << 2U) ^
         (procedureIntervalTicks << 3U) ^
         (subeventDelayTicks << 6U) ^
         (static_cast<uint32_t>(peerGapTicks) << 20U) ^
         (static_cast<uint32_t>(intervalSelector) << 12U);
}

inline void writeVolatileLe16(volatile uint8_t* data, uint16_t value) {
  if (data == nullptr) {
    return;
  }
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

inline void writeVolatileLe32(volatile uint8_t* data, uint32_t value) {
  if (data == nullptr) {
    return;
  }
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  data[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  data[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

bool writeBleConnectionBootHandoff(
    volatile Nrf54l15VprTransportHostShared* sharedHost,
    const VprBleConnectionSharedState& state) {
  if (sharedHost == nullptr || !state.connected || state.connHandle == 0U ||
      NRF54L15_VPR_BLE_CONN_HANDOFF_LEN > sizeof(sharedHost->hostData)) {
    return false;
  }

  memset(const_cast<uint8_t*>(
             &const_cast<Nrf54l15VprTransportHostShared*>(sharedHost)->hostData[0]),
         0, sizeof(sharedHost->hostData));
  sharedHost->reserved = NRF54L15_VPR_BLE_CONN_HANDOFF_COOKIE;
  sharedHost->hostData[0] = state.connected ? 1U : 0U;
  writeVolatileLe16(&sharedHost->hostData[1], state.connHandle);
  sharedHost->hostData[3] = state.role;
  sharedHost->hostData[4] = state.encrypted ? 1U : 0U;
  writeVolatileLe16(&sharedHost->hostData[5], state.intervalUnits);
  writeVolatileLe16(&sharedHost->hostData[7], state.latency);
  writeVolatileLe16(&sharedHost->hostData[9], state.supervisionTimeout);
  sharedHost->hostData[11] = state.txPhy;
  sharedHost->hostData[12] = state.rxPhy;
  writeVolatileLe32(&sharedHost->hostData[13], state.eventCount);
  __DMB();
  __DSB();
  return true;
}

uint8_t csFrequencyOffsetMHz(uint8_t channel) {
  if (channel <= 10U) {
    return static_cast<uint8_t>(4U + (2U * channel));
  }
  return static_cast<uint8_t>(6U + (2U * channel));
}

void encodePctSampleBytes(int16_t i, int16_t q, uint8_t outPct[3]) {
  if (outPct == nullptr) {
    return;
  }

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

bool appendMode2DemoStep(uint8_t* buffer,
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

inline bool validDataChannel(uint8_t channelIndex);
inline bool validCsChannelIndex(uint8_t channelIndex);

inline bool bleCsEventCounterReached(uint16_t current, uint16_t target) {
  return static_cast<int16_t>(current - target) >= 0;
}

bool bleCsEventIsConnectedSweepAck(const BleConnectionEvent& evt,
                                   uint8_t ackReason) {
  return (evt.llControlOpcode == kBleCsLlCtrlAbort ||
          evt.llControlOpcode == kBleCsLlCtrlTerminate) &&
         evt.payload != nullptr &&
         evt.payloadLength >= kBleCsLlControlTerminateAbortPduLength &&
         evt.payload[2U] == ackReason;
}

uint8_t qualityFromRawTone(const BleCsToneSample& tone) {
  if (!tone.valid) {
    return kBleCsToneQualityUnavailable;
  }
  return kBleCsToneQualityHigh;
}

bool appendMode2ToneStep(uint8_t* buffer,
                         size_t maxLen,
                         size_t* offset,
                         uint8_t channel,
                         uint8_t antennaPermutationIndex,
                         const BleCsToneSample& tone) {
  if (buffer == nullptr || offset == nullptr || *offset > maxLen ||
      (maxLen - *offset) < 8U) {
    return false;
  }
  if (!validCsChannelIndex(channel) || !tone.valid) {
    return false;
  }

  buffer[*offset + 0U] = kBleCsMainMode2;
  buffer[*offset + 1U] = channel;
  buffer[*offset + 2U] = 5U;
  buffer[*offset + 3U] = antennaPermutationIndex;
  encodePctSampleBytes(tone.i, tone.q, &buffer[*offset + 4U]);
  buffer[*offset + 7U] =
      static_cast<uint8_t>(qualityFromRawTone(tone) |
                           (kBleCsToneExtensionNone << 4U));
  *offset += 8U;
  return true;
}

bool buildHciInitialEvent(uint8_t* out,
                          size_t maxLen,
                          uint16_t connHandle,
                          uint8_t configId,
                          uint16_t startAclConnEventCounter,
                          uint16_t procedureCounter,
                          uint8_t numAntennaPaths,
                          uint8_t numStepsReported,
                          const uint8_t* stepBytes,
                          size_t stepLen,
                          bool partial) {
  if (out == nullptr || stepBytes == nullptr || maxLen < (15U + stepLen)) {
    return false;
  }
  writeLe16(out + 0U, connHandle);
  out[2U] = configId;
  writeLe16(out + 3U, startAclConnEventCounter);
  writeLe16(out + 5U, procedureCounter);
  writeLe16(out + 7U, 0U);
  out[9U] = 0U;
  out[10U] = partial ? kBleCsProcedureDonePartial : kBleCsProcedureDoneComplete;
  out[11U] = partial ? kBleCsSubeventDonePartial : kBleCsSubeventDoneComplete;
  out[12U] = 0U;
  out[13U] = numAntennaPaths;
  out[14U] = numStepsReported;
  memcpy(out + 15U, stepBytes, stepLen);
  return true;
}

bool buildHciContinueEvent(uint8_t* out,
                           size_t maxLen,
                           uint16_t connHandle,
                           uint8_t configId,
                           uint8_t numAntennaPaths,
                           uint8_t numStepsReported,
                           const uint8_t* stepBytes,
                           size_t stepLen,
                           bool partial) {
  if (out == nullptr || stepBytes == nullptr || maxLen < (8U + stepLen)) {
    return false;
  }
  writeLe16(out + 0U, connHandle);
  out[2U] = configId;
  out[3U] = partial ? kBleCsProcedureDonePartial : kBleCsProcedureDoneComplete;
  out[4U] = partial ? kBleCsSubeventDonePartial : kBleCsSubeventDoneComplete;
  out[5U] = 0U;
  out[6U] = numAntennaPaths;
  out[7U] = numStepsReported;
  memcpy(out + 8U, stepBytes, stepLen);
  return true;
}

bool buildH4LeMetaEvent(uint8_t* out,
                        size_t maxLen,
                        uint8_t subeventCode,
                        const uint8_t* payload,
                        size_t payloadLen) {
  if (out == nullptr || payload == nullptr || maxLen < (4U + payloadLen)) {
    return false;
  }
  out[0U] = kBleHciPacketTypeEvent;
  out[1U] = kBleHciEvtLeMeta;
  out[2U] = static_cast<uint8_t>(1U + payloadLen);
  out[3U] = subeventCode;
  memcpy(out + 4U, payload, payloadLen);
  return true;
}

bool buildH4VendorPeerResultSourceEvent(uint8_t* out,
                                        size_t maxLen,
                                        uint8_t configId,
                                        uint16_t procedureCounter,
                                        size_t* outLen) {
  if (out == nullptr || outLen == nullptr || maxLen < 7U) {
    return false;
  }
  out[0U] = kBleHciPacketTypeEvent;
  out[1U] = kBleHciEvtVendor;
  out[2U] = 4U;
  out[3U] = kBleCsVprVendorEvtPeerResultSource;
  out[4U] = configId;
  writeLe16(out + 5U, procedureCounter);
  *outLen = 7U;
  return true;
}

[[maybe_unused]] bool buildDemoPeerResultPackets(uint16_t connHandle,
                                uint8_t configId,
                                uint16_t procedureCounter,
                                const BleCsControllerVprBuiltInPeerDemoConfig& config,
                                uint8_t* initPacket,
                                size_t initPacketMaxLen,
                                size_t* initPacketLen,
                                uint8_t* contPacket,
                                size_t contPacketMaxLen,
                                size_t* contPacketLen) {
  if (initPacket == nullptr || initPacketLen == nullptr || contPacket == nullptr ||
      contPacketLen == nullptr) {
    return false;
  }

  *initPacketLen = 0U;
  *contPacketLen = 0U;

  const uint8_t channelCount =
      (config.channelCount <= sizeof(config.channels)) ? config.channelCount
                                                       : sizeof(config.channels);
  if (channelCount == 0U) {
    return false;
  }

  uint8_t peerSteps[64] = {0};
  size_t peerLen = 0U;
  for (uint8_t i = 0U; i < channelCount; ++i) {
    const uint8_t channel = config.channels[i];
    const float freqHz =
        (2400.0f + static_cast<float>(csFrequencyOffsetMHz(channel))) * 1000000.0f;
    const float theta = -((4.0f * kPi * config.distanceMeters * freqHz) /
                          kSpeedOfLightMetersPerSecond);
    const int16_t peerI =
        static_cast<int16_t>(lroundf(cosf(theta) * config.amplitude));
    const int16_t peerQ =
        static_cast<int16_t>(lroundf(sinf(theta) * config.amplitude));
    const uint8_t csChannelIndex = static_cast<uint8_t>(
        csFrequencyOffsetMHz(channel) - 2U);
    if (!appendMode2DemoStep(peerSteps, sizeof(peerSteps), &peerLen,
                             csChannelIndex, peerI, peerQ)) {
      return false;
    }
  }

  const size_t splitLen = (peerLen > 16U) ? 16U : peerLen;
  const size_t contLen = peerLen - splitLen;
  const bool partial = contLen > 0U;
  const uint16_t startAclConnEventCounter = 0x1234U;
  const uint8_t numAntennaPaths = 1U;
  const uint8_t initSteps = static_cast<uint8_t>(splitLen / 8U);
  const uint8_t contSteps = static_cast<uint8_t>(contLen / 8U);
  uint8_t initPayload[64] = {0};
  uint8_t contPayload[64] = {0};

  if (!buildHciInitialEvent(initPayload, sizeof(initPayload), connHandle, configId,
                            startAclConnEventCounter, procedureCounter,
                            numAntennaPaths, initSteps, peerSteps, splitLen, partial) ||
      !buildH4LeMetaEvent(initPacket, initPacketMaxLen, kBleCsHciEvtSubeventResult,
                          initPayload, 15U + splitLen)) {
    return false;
  }
  *initPacketLen = 4U + 15U + splitLen;

  if (contLen == 0U) {
    return true;
  }

  if (!buildHciContinueEvent(contPayload, sizeof(contPayload), connHandle, configId,
                             numAntennaPaths, contSteps, peerSteps + splitLen, contLen,
                             false) ||
      !buildH4LeMetaEvent(contPacket, contPacketMaxLen,
                          kBleCsHciEvtSubeventResultContinue, contPayload,
                          8U + contLen)) {
    *initPacketLen = 0U;
    return false;
  }
  *contPacketLen = 4U + 8U + contLen;
  return true;
}

bool decodeHciEventFrame(const uint8_t* packet,
                         size_t packetLen,
                         uint8_t* outEventCode,
                         const uint8_t** outParams,
                         size_t* outParamsLen) {
  if (packet == nullptr || outEventCode == nullptr || outParams == nullptr ||
      outParamsLen == nullptr) {
    return false;
  }

  if (packetLen >= 3U && packet[0] == kBleHciPacketTypeEvent) {
    const size_t paramsLen = packet[2U];
    if (packetLen < (3U + paramsLen)) {
      return false;
    }
    *outEventCode = packet[1U];
    *outParams = packet + 3U;
    *outParamsLen = paramsLen;
    return true;
  }

  if (packetLen >= 2U) {
    const size_t paramsLen = packet[1U];
    if (packetLen < (2U + paramsLen)) {
      return false;
    }
    *outEventCode = packet[0U];
    *outParams = packet + 2U;
    *outParamsLen = paramsLen;
    return true;
  }

  return false;
}

inline int16_t readLe16Signed(const uint8_t* data) {
  return static_cast<int16_t>(readLe16(data));
}

inline bool validDataChannel(uint8_t channelIndex) {
  return channelIndex <= 36U;
}

inline bool validCsChannelIndex(uint8_t channelIndex) {
  return channelIndex >= 2U && channelIndex <= 76U &&
         channelIndex != 23U && channelIndex != 24U &&
         channelIndex != 25U;
}

float csChannelFrequencyHz(uint8_t channelIndex) {
  return (2402.0f + static_cast<float>(channelIndex)) * 1000000.0f;
}

inline bool validLogicalChannel(uint8_t channelIndex) {
  return (channelIndex <= 36U) || (channelIndex >= 37U && channelIndex <= 39U);
}

uint8_t logicalChannelToFrequency(uint8_t channelIndex) {
  if (channelIndex <= 36U) {
    if (channelIndex <= 10U) {
      return static_cast<uint8_t>(4U + (2U * channelIndex));
    }
    return static_cast<uint8_t>(6U + (2U * channelIndex));
  }

  switch (channelIndex) {
    case 37U:
      return 2U;
    case 38U:
      return 26U;
    case 39U:
    default:
      return 80U;
  }
}

uint32_t bleDataWhiteValue(uint8_t channelIndex) {
  const uint32_t iv = static_cast<uint32_t>(0x40U | (channelIndex & 0x3FU));
  const uint32_t poly = 0x89UL;
  return ((poly << RADIO_DATAWHITE_POLY_Pos) & RADIO_DATAWHITE_POLY_Msk) |
         ((iv << RADIO_DATAWHITE_IV_Pos) & RADIO_DATAWHITE_IV_Msk);
}

uint32_t accessAddressBase(uint32_t accessAddress) {
  return (accessAddress << 8U);
}

uint32_t accessAddressPrefix(uint32_t accessAddress) {
  return ((accessAddress >> 24U) & 0xFFU);
}

inline void channelMapBitSetVal(uint8_t* channelMap, uint8_t bit, uint8_t value) {
  channelMap[bit / 8U] = static_cast<uint8_t>(
      (channelMap[bit / 8U] & ~static_cast<uint8_t>(1U << (bit % 8U))) |
      ((value & 0x1U) << (bit % 8U)));
}

uint32_t txPowerRegFromDbm(int8_t dbm) {
  if (dbm >= 8) {
    return RADIO_TXPOWER_TXPOWER_Pos8dBm;
  }
  if (dbm >= 7) {
    return RADIO_TXPOWER_TXPOWER_Pos7dBm;
  }
  if (dbm >= 6) {
    return RADIO_TXPOWER_TXPOWER_Pos6dBm;
  }
  if (dbm >= 5) {
    return RADIO_TXPOWER_TXPOWER_Pos5dBm;
  }
  if (dbm >= 4) {
    return RADIO_TXPOWER_TXPOWER_Pos4dBm;
  }
  if (dbm >= 3) {
    return RADIO_TXPOWER_TXPOWER_Pos3dBm;
  }
  if (dbm >= 2) {
    return RADIO_TXPOWER_TXPOWER_Pos2dBm;
  }
  if (dbm >= 1) {
    return RADIO_TXPOWER_TXPOWER_Pos1dBm;
  }
  if (dbm >= 0) {
    return RADIO_TXPOWER_TXPOWER_0dBm;
  }
  if (dbm >= -1) {
    return RADIO_TXPOWER_TXPOWER_Neg1dBm;
  }
  if (dbm >= -2) {
    return RADIO_TXPOWER_TXPOWER_Neg2dBm;
  }
  if (dbm >= -3) {
    return RADIO_TXPOWER_TXPOWER_Neg3dBm;
  }
  if (dbm >= -4) {
    return RADIO_TXPOWER_TXPOWER_Neg4dBm;
  }
  if (dbm >= -5) {
    return RADIO_TXPOWER_TXPOWER_Neg5dBm;
  }
  if (dbm >= -6) {
    return RADIO_TXPOWER_TXPOWER_Neg6dBm;
  }
  if (dbm >= -7) {
    return RADIO_TXPOWER_TXPOWER_Neg7dBm;
  }
  if (dbm >= -8) {
    return RADIO_TXPOWER_TXPOWER_Neg8dBm;
  }
  if (dbm >= -9) {
    return RADIO_TXPOWER_TXPOWER_Neg9dBm;
  }
  if (dbm >= -10) {
    return RADIO_TXPOWER_TXPOWER_Neg10dBm;
  }
  if (dbm >= -12) {
    return RADIO_TXPOWER_TXPOWER_Neg12dBm;
  }
  if (dbm >= -14) {
    return RADIO_TXPOWER_TXPOWER_Neg14dBm;
  }
  if (dbm >= -16) {
    return RADIO_TXPOWER_TXPOWER_Neg16dBm;
  }
  if (dbm >= -18) {
    return RADIO_TXPOWER_TXPOWER_Neg18dBm;
  }
  if (dbm >= -20) {
    return RADIO_TXPOWER_TXPOWER_Neg20dBm;
  }
  if (dbm >= -28) {
    return RADIO_TXPOWER_TXPOWER_Neg28dBm;
  }
  if (dbm >= -40) {
    return RADIO_TXPOWER_TXPOWER_Neg40dBm;
  }
  return RADIO_TXPOWER_TXPOWER_Neg46dBm;
}

void clearRadioEvents(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return;
  }

  radio->EVENTS_READY = 0U;
  radio->EVENTS_TXREADY = 0U;
  radio->EVENTS_RXREADY = 0U;
  radio->EVENTS_ADDRESS = 0U;
  radio->EVENTS_PAYLOAD = 0U;
  radio->EVENTS_END = 0U;
  radio->EVENTS_PHYEND = 0U;
  radio->EVENTS_DISABLED = 0U;
  radio->EVENTS_CRCOK = 0U;
  radio->EVENTS_CRCERROR = 0U;
  radio->EVENTS_CTEPRESENT = 0U;
  radio->EVENTS_AUXDATADMAEND = 0U;
  radio->EVENTS_CSTONESEND = 0U;
}

void clearRadioEventsPreserveCstones(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return;
  }

  radio->EVENTS_READY = 0U;
  radio->EVENTS_TXREADY = 0U;
  radio->EVENTS_RXREADY = 0U;
  radio->EVENTS_ADDRESS = 0U;
  radio->EVENTS_PAYLOAD = 0U;
  radio->EVENTS_END = 0U;
  radio->EVENTS_PHYEND = 0U;
  radio->EVENTS_DISABLED = 0U;
  radio->EVENTS_CRCOK = 0U;
  radio->EVENTS_CRCERROR = 0U;
  radio->EVENTS_CTEPRESENT = 0U;
  radio->EVENTS_AUXDATADMAEND = 0U;
}

void detachRawRadioAutomation(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return;
  }

  radio->SUBSCRIBE_TXEN = 0U;
  radio->SUBSCRIBE_RXEN = 0U;
  radio->SUBSCRIBE_START = 0U;
  radio->SUBSCRIBE_STOP = 0U;
  radio->SUBSCRIBE_DISABLE = 0U;
  radio->SUBSCRIBE_RSSISTART = 0U;
  radio->SUBSCRIBE_BCSTART = 0U;
  radio->SUBSCRIBE_BCSTOP = 0U;
  radio->SUBSCRIBE_EDSTART = 0U;
  radio->SUBSCRIBE_EDSTOP = 0U;
  radio->SUBSCRIBE_CCASTART = 0U;
  radio->SUBSCRIBE_CCASTOP = 0U;
  radio->SUBSCRIBE_AUXDATADMASTART = 0U;
  radio->SUBSCRIBE_AUXDATADMASTOP = 0U;
  radio->SUBSCRIBE_PLLEN = 0U;
  radio->SUBSCRIBE_CSTONESSTART = 0U;
  radio->SUBSCRIBE_SOFTRESET = 0U;

  radio->PUBLISH_READY = 0U;
  radio->PUBLISH_TXREADY = 0U;
  radio->PUBLISH_RXREADY = 0U;
  radio->PUBLISH_ADDRESS = 0U;
  radio->PUBLISH_FRAMESTART = 0U;
  radio->PUBLISH_PAYLOAD = 0U;
  radio->PUBLISH_END = 0U;
  radio->PUBLISH_PHYEND = 0U;
  radio->PUBLISH_DISABLED = 0U;
  radio->PUBLISH_DEVMATCH = 0U;
  radio->PUBLISH_DEVMISS = 0U;
  radio->PUBLISH_CRCOK = 0U;
  radio->PUBLISH_CRCERROR = 0U;
  radio->PUBLISH_BCMATCH = 0U;
  radio->PUBLISH_EDEND = 0U;
  radio->PUBLISH_EDSTOPPED = 0U;
  radio->PUBLISH_CCAIDLE = 0U;
  radio->PUBLISH_CCABUSY = 0U;
  radio->PUBLISH_CCASTOPPED = 0U;
  radio->PUBLISH_RATEBOOST = 0U;
  radio->PUBLISH_MHRMATCH = 0U;
  radio->PUBLISH_SYNC = 0U;
  radio->PUBLISH_CTEPRESENT = 0U;
  radio->PUBLISH_PLLREADY = 0U;
  radio->PUBLISH_RXADDRESS = 0U;
  radio->PUBLISH_AUXDATADMAEND = 0U;
  radio->PUBLISH_CSTONESEND = 0U;

  radio->INTENCLR00 = 0xFFFFFFFFUL;
  radio->INTENCLR01 = 0xFFFFFFFFUL;
  radio->INTENCLR10 = 0xFFFFFFFFUL;
  radio->INTENCLR11 = 0xFFFFFFFFUL;
  NVIC_ClearPendingIRQ(RADIO_0_IRQn);
}

bool waitForFlag(volatile uint32_t* flag, uint32_t budgetUs) {
  if (flag == nullptr) {
    return false;
  }

  const uint32_t startUs = micros();
  uint8_t divider = kMicrosPollDivider;
  uint32_t spins = kSpinLimit;
  while (spins-- > 0U) {
    if (*flag != 0U) {
      return true;
    }

    if (--divider == 0U) {
      divider = kMicrosPollDivider;
      if ((budgetUs > 0U) &&
          (static_cast<uint32_t>(micros() - startUs) >= budgetUs)) {
        break;
      }
    }
  }

  return (*flag != 0U);
}

bool stopAndDisableAuxDataDma(NRF_RADIO_Type* radio, uint32_t budgetUs) {
  if (radio == nullptr) {
    return false;
  }
  const bool enabled = radio->AUXDATADMA[0].ENABLE != 0U ||
                       radio->AUXDATADMA[1].ENABLE != 0U;
  if (enabled && radio->EVENTS_AUXDATADMAEND == 0U) {
    radio->TASKS_AUXDATADMASTOP =
        RADIO_TASKS_AUXDATADMASTOP_TASKS_AUXDATADMASTOP_Trigger;
    if (!waitForFlag(&radio->EVENTS_AUXDATADMAEND, budgetUs)) {
      return false;
    }
  }
  for (uint8_t i = 0U; i < 2U; ++i) {
    radio->AUXDATADMA[i].ENABLE =
        (RADIO_AUXDATADMA_ENABLE_ENABLE_Disabled
         << RADIO_AUXDATADMA_ENABLE_ENABLE_Pos) &
        RADIO_AUXDATADMA_ENABLE_ENABLE_Msk;
    radio->AUXDATA.CNF[i] = 0U;
  }
  __DSB();
  return true;
}

void waitElapsedMicros(uint32_t waitUs) {
  if (waitUs == 0U) {
    return;
  }

  const uint32_t startUs = micros();
  while (static_cast<uint32_t>(micros() - startUs) < waitUs) {
    __asm volatile("nop");
  }
}

bool waitForCrcDone(NRF_RADIO_Type* radio, uint32_t budgetUs) {
  if (radio == nullptr) {
    return false;
  }

  const uint32_t startUs = micros();
  uint8_t divider = kMicrosPollDivider;
  uint32_t spins = kSpinLimit;
  while (spins-- > 0U) {
    if ((radio->EVENTS_CRCOK != 0U) || (radio->EVENTS_CRCERROR != 0U)) {
      return true;
    }

    if (--divider == 0U) {
      divider = kMicrosPollDivider;
      if ((budgetUs > 0U) &&
          (static_cast<uint32_t>(micros() - startUs) >= budgetUs)) {
        break;
      }
    }
  }

  return ((radio->EVENTS_CRCOK != 0U) || (radio->EVENTS_CRCERROR != 0U));
}

bool waitForRadioDisabled(NRF_RADIO_Type* radio, uint32_t budgetUs) {
  if (radio == nullptr) {
    return false;
  }

  const uint32_t startUs = micros();
  uint8_t divider = kMicrosPollDivider;
  uint32_t spins = kSpinLimit;
  while (spins-- > 0U) {
    const uint32_t state =
        (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      return true;
    }

    if (--divider == 0U) {
      divider = kMicrosPollDivider;
      if ((budgetUs > 0U) &&
          (static_cast<uint32_t>(micros() - startUs) >= budgetUs)) {
        break;
      }
    }
  }

  const uint32_t state =
      (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  return state == RADIO_STATE_STATE_Disabled;
}

bool waitForRadioPhyEnd(NRF_RADIO_Type* radio, uint32_t budgetUs) {
  if (radio == nullptr) {
    return false;
  }

  const uint32_t startUs = micros();
  uint8_t divider = kMicrosPollDivider;
  uint32_t spins = kSpinLimit;
  while (spins-- > 0U) {
    if (radio->EVENTS_PHYEND != 0U) {
      return true;
    }

    if (--divider == 0U) {
      divider = kMicrosPollDivider;
      if ((budgetUs > 0U) &&
          (static_cast<uint32_t>(micros() - startUs) >= budgetUs)) {
        break;
      }
    }
  }

  return (radio->EVENTS_PHYEND != 0U);
}

int8_t radioRssiDbm(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return 0;
  }

  const uint8_t raw =
      static_cast<uint8_t>((radio->RSSISAMPLE & RADIO_RSSISAMPLE_RSSISAMPLE_Msk) >>
                           RADIO_RSSISAMPLE_RSSISAMPLE_Pos);
  return -static_cast<int8_t>(raw);
}

void sortFloats(float* values, size_t count) {
  if (values == nullptr || count < 2U) {
    return;
  }

  for (size_t i = 1U; i < count; ++i) {
    const float key = values[i];
    size_t j = i;
    while (j > 0U && values[j - 1U] > key) {
      values[j] = values[j - 1U];
      --j;
    }
    values[j] = key;
  }
}

float medianInPlace(float* values, size_t count) {
  if (values == nullptr || count == 0U) {
    return 0.0f;
  }

  sortFloats(values, count);
  if ((count & 0x1U) == 0U) {
    const size_t upper = count / 2U;
    return (values[upper - 1U] + values[upper]) * 0.5f;
  }
  return values[count / 2U];
}

bool fitTheilSenLine(const float* freqsHz,
                     const float* phases,
                     size_t count,
                     float* outSlope,
                     float* outIntercept) {
  if (freqsHz == nullptr || phases == nullptr || outSlope == nullptr ||
      outIntercept == nullptr || count < 2U || count > kMaxCsChannels) {
    return false;
  }

  float slopes[kMaxSlopePairs] = {0.0f};
  size_t slopeCount = 0U;
  for (size_t i = 0U; i + 1U < count; ++i) {
    for (size_t j = i + 1U; j < count; ++j) {
      const float df = freqsHz[j] - freqsHz[i];
      if (df == 0.0f) {
        continue;
      }
      slopes[slopeCount++] = (phases[j] - phases[i]) / df;
    }
  }
  if (slopeCount == 0U) {
    return false;
  }

  const float slope = medianInPlace(slopes, slopeCount);
  float intercepts[kMaxCsChannels] = {0.0f};
  for (size_t i = 0U; i < count; ++i) {
    intercepts[i] = phases[i] - (slope * freqsHz[i]);
  }

  *outSlope = slope;
  *outIntercept = medianInPlace(intercepts, count);
  return true;
}

bool fitWeightedLine(const float* freqsHz,
                     const float* phases,
                     const float* weights,
                     size_t count,
                     float* outSlope,
                     float* outIntercept) {
  if (freqsHz == nullptr || phases == nullptr || weights == nullptr ||
      outSlope == nullptr || outIntercept == nullptr || count < 2U ||
      count > kMaxCsControllerStepSamples) {
    return false;
  }

  float weightSum = 0.0f;
  float weightedX = 0.0f;
  float weightedY = 0.0f;
  for (size_t i = 0U; i < count; ++i) {
    const float weight = fmaxf(weights[i], 0.0001f);
    weightSum += weight;
    weightedX += weight * freqsHz[i];
    weightedY += weight * phases[i];
  }
  if (!(weightSum > 0.0f)) {
    return false;
  }

  const float xMean = weightedX / weightSum;
  const float yMean = weightedY / weightSum;
  float numerator = 0.0f;
  float denominator = 0.0f;
  for (size_t i = 0U; i < count; ++i) {
    const float weight = fmaxf(weights[i], 0.0001f);
    const float dx = freqsHz[i] - xMean;
    numerator += weight * dx * (phases[i] - yMean);
    denominator += weight * dx * dx;
  }
  if (!(denominator > 0.0f)) {
    return false;
  }

  const float slope = numerator / denominator;
  *outSlope = slope;
  *outIntercept = yMean - (slope * xMean);
  return true;
}

bool estimateMedianAndMad(const float* values,
                          size_t count,
                          float* outMedian,
                          float* outMad) {
  if (values == nullptr || outMedian == nullptr || outMad == nullptr ||
      count == 0U || count > kMaxCsChannels) {
    return false;
  }

  float scratch[kMaxCsChannels] = {0.0f};
  for (size_t i = 0U; i < count; ++i) {
    scratch[i] = values[i];
  }
  const float median = medianInPlace(scratch, count);

  for (size_t i = 0U; i < count; ++i) {
    scratch[i] = fabsf(values[i] - median);
  }
  *outMedian = median;
  *outMad = medianInPlace(scratch, count);
  return true;
}

bool estimateMedianAndMadWide(const float* values,
                              size_t count,
                              float* outMedian,
                              float* outMad) {
  if (values == nullptr || outMedian == nullptr || outMad == nullptr ||
      count == 0U || count > kMaxCsControllerStepSamples) {
    return false;
  }

  float scratch[kMaxCsControllerStepSamples] = {0.0f};
  for (size_t i = 0U; i < count; ++i) {
    scratch[i] = values[i];
  }
  const float median = medianInPlace(scratch, count);
  for (size_t i = 0U; i < count; ++i) {
    scratch[i] = fabsf(values[i] - median);
  }
  *outMedian = median;
  *outMad = medianInPlace(scratch, count);
  return true;
}

void sortPhaseSamplesWithQuality(float* freqsHz,
                                 float* phases,
                                 float* quality,
                                 size_t count) {
  if (freqsHz == nullptr || phases == nullptr || quality == nullptr ||
      count < 2U) {
    return;
  }

  for (size_t i = 1U; i < count; ++i) {
    const float freq = freqsHz[i];
    const float phase = phases[i];
    const float score = quality[i];
    size_t j = i;
    while (j > 0U && freqsHz[j - 1U] > freq) {
      freqsHz[j] = freqsHz[j - 1U];
      phases[j] = phases[j - 1U];
      quality[j] = quality[j - 1U];
      --j;
    }
    freqsHz[j] = freq;
    phases[j] = phase;
    quality[j] = score;
  }
}

float toneQualityScore(const BleCsToneSample& local, const BleCsToneSample& peer) {
  if (!local.valid || !peer.valid) {
    return 0.0f;
  }

  const float localMag = static_cast<float>(local.magnitude);
  const float peerMag = static_cast<float>(peer.magnitude);
  const float localStd = static_cast<float>(local.magnitudeStd);
  const float peerStd = static_cast<float>(peer.magnitudeStd);
  const float denom = 1.0f + localStd + peerStd;
  const float minMagnitude = fminf(localMag, peerMag);
  if (denom <= 0.0f || minMagnitude <= 0.0f) {
    return 0.0f;
  }

  return minMagnitude / denom;
}

bool estimateAdjacentPhaseDistance(const float* freqsHz,
                                   const float* phases,
                                   const float* quality,
                                   size_t count,
                                   float* outDistanceMeters,
                                   float* outCoherence,
                                   uint8_t* outPairCount) {
  if (freqsHz == nullptr || phases == nullptr || quality == nullptr ||
      outDistanceMeters == nullptr || outCoherence == nullptr ||
      outPairCount == nullptr) {
    return false;
  }

  *outDistanceMeters = NAN;
  *outCoherence = 0.0f;
  *outPairCount = 0U;
  if (count < 2U) {
    return false;
  }

  float sumI = 0.0f;
  float sumQ = 0.0f;
  float weightSum = 0.0f;
  uint8_t pairCount = 0U;
  for (size_t i = 1U; i < count; ++i) {
    const float spacingHz = freqsHz[i] - freqsHz[i - 1U];
    if (!isfinite(spacingHz) ||
        fabsf(spacingHz - kAdjacentChannelSpacingHz) >
            kAdjacentChannelSpacingToleranceHz) {
      continue;
    }

    const float weight = fminf(quality[i], quality[i - 1U]);
    if (!isfinite(weight) || weight <= 0.0f) {
      continue;
    }

    const float phaseDifference = phases[i] - phases[i - 1U];
    sumI += weight * cosf(phaseDifference);
    sumQ += weight * sinf(phaseDifference);
    weightSum += weight;
    ++pairCount;
  }

  *outPairCount = pairCount;
  if (pairCount < kMinimumAdjacentPhasePairs || weightSum <= 0.0f) {
    return false;
  }

  const float resultant = hypotf(sumI, sumQ);
  const float coherence = fminf(1.0f, resultant / weightSum);
  *outCoherence = coherence;
  if (!isfinite(coherence) || coherence < kMinimumAdjacentPhaseCoherence) {
    return false;
  }

  const float meanPhaseDifference = atan2f(sumQ, sumI);
  const float distance = fabsf(
      -(kSpeedOfLightMetersPerSecond * meanPhaseDifference) /
      (4.0f * kPi * kAdjacentChannelSpacingHz));
  if (!isfinite(distance) || distance <= 0.0f) {
    return false;
  }

  *outDistanceMeters = distance;
  return true;
}

float stepToneQualityScore(uint8_t localQuality, uint8_t peerQuality) {
  auto scoreOne = [](uint8_t quality) -> float {
    switch (quality) {
      case kBleCsToneQualityHigh:
        return 1.0f;
      case kBleCsToneQualityMedium:
        return 0.75f;
      case kBleCsToneQualityLow:
        return 0.25f;
      case kBleCsToneQualityUnavailable:
      default:
        return 0.0f;
    }
  };
  return fminf(scoreOne(localQuality), scoreOne(peerQuality));
}

bool controllerPhaseDistanceEstimate(const BleCsControllerPhasePair* pairs,
                                     size_t pairCount,
                                     BleCsEstimate* outEstimate) {
  if (pairs == nullptr || outEstimate == nullptr || pairCount == 0U) {
    return false;
  }

  float freqsHz[kMaxCsControllerStepSamples] = {0.0f};
  float phases[kMaxCsControllerStepSamples] = {0.0f};
  float quality[kMaxCsControllerStepSamples] = {0.0f};
  size_t sampleCount = 0U;
  for (size_t i = 0U; i < pairCount && sampleCount < kMaxCsControllerStepSamples; ++i) {
    if (pairs[i].failed || !pairs[i].localPresent || !pairs[i].peerPresent ||
        !validCsChannelIndex(pairs[i].channel) ||
        (pairs[i].local.i == 0 && pairs[i].local.q == 0) ||
        (pairs[i].peer.i == 0 && pairs[i].peer.q == 0)) {
      if (outEstimate->rejectedLowQualityChannels < 0xFFU) {
        ++outEstimate->rejectedLowQualityChannels;
      }
      continue;
    }
    const float localI = static_cast<float>(pairs[i].local.i);
    const float localQ = static_cast<float>(pairs[i].local.q);
    const float peerI = static_cast<float>(pairs[i].peer.i);
    const float peerQ = static_cast<float>(pairs[i].peer.q);
    const float combI = (localI * peerI) - (localQ * peerQ);
    const float combQ = (localI * peerQ) + (peerI * localQ);
    freqsHz[sampleCount] = csChannelFrequencyHz(pairs[i].channel);
    phases[sampleCount] = atan2f(combQ, combI);
    quality[sampleCount] =
        stepToneQualityScore(pairs[i].localQuality, pairs[i].peerQuality);
    ++sampleCount;
  }

  outEstimate->totalToneChannels = static_cast<uint8_t>(
      (sampleCount <= 255U) ? sampleCount : 255U);
  if (sampleCount < kMinimumControllerPbrChannels) {
    return false;
  }

  sortPhaseSamplesWithQuality(freqsHz, phases, quality, sampleCount);
  for (size_t i = 1U; i < sampleCount; ++i) {
    if (freqsHz[i] <= freqsHz[i - 1U]) {
      // The estimator currently supports one single-antenna PCT pair per
      // channel. Repeated frequencies need antenna/repetition metadata before
      // they can be combined without manufacturing an independent sample.
      return false;
    }
  }
  if ((freqsHz[sampleCount - 1U] - freqsHz[0U]) <
      kMinimumControllerPbrSpanHz) {
    return false;
  }
  for (size_t i = 1U; i < sampleCount; ++i) {
    const float prev = phases[i - 1U];
    while ((phases[i] - prev) > kPi) {
      phases[i] -= 2.0f * kPi;
    }
    while ((phases[i] - prev) < -kPi) {
      phases[i] += 2.0f * kPi;
    }
  }

  float medianQuality = 0.0f;
  float qualityMad = 0.0f;
  if (estimateMedianAndMadWide(quality, sampleCount, &medianQuality, &qualityMad)) {
    outEstimate->medianToneQuality = medianQuality;
  }

  float slope = 0.0f;
  float intercept = 0.0f;
  if (!fitWeightedLine(freqsHz, phases, quality, sampleCount, &slope, &intercept)) {
    return false;
  }

  float residuals[kMaxCsControllerStepSamples] = {0.0f};
  float residualAccum = 0.0f;
  for (size_t i = 0U; i < sampleCount; ++i) {
    residuals[i] = phases[i] - (intercept + (slope * freqsHz[i]));
    residualAccum += residuals[i] * residuals[i];
  }

  const float phaseDistance =
      fabsf(-(kSpeedOfLightMetersPerSecond * slope) / (4.0f * kPi));
  const float residualVariance =
      residualAccum / static_cast<float>(sampleCount);
  outEstimate->residualVariance = residualVariance;
  if (!isfinite(phaseDistance) || phaseDistance <= 0.0f ||
      !isfinite(residualVariance) ||
      residualVariance > kMaximumControllerPbrResidualVariance) {
    return false;
  }

  outEstimate->usedChannels = static_cast<uint8_t>(
      (sampleCount <= 255U) ? sampleCount : 255U);
  outEstimate->phaseSlopeDistanceMeters = phaseDistance;
  outEstimate->distanceMeters = phaseDistance;
  outEstimate->slopeRadPerHz = slope;
  return true;
}

bool controllerRttDistanceEstimate(const BleCsControllerRttPair* pairs,
                                   size_t pairCount,
                                   BleCsEstimate* outEstimate) {
  if (pairs == nullptr || outEstimate == nullptr || pairCount == 0U) {
    return false;
  }

  float rttDistances[kMaxCsControllerStepSamples] = {0.0f};
  size_t rttCount = 0U;
  for (size_t i = 0U; i < pairCount && rttCount < kMaxCsControllerStepSamples; ++i) {
    if (pairs[i].failed || !pairs[i].initiatorPresent || !pairs[i].reflectorPresent) {
      continue;
    }
    const int32_t roundTripHalfNs =
        static_cast<int32_t>(pairs[i].toaTodInitiator) -
        static_cast<int32_t>(pairs[i].todToaReflector);
    if (roundTripHalfNs <= 0) {
      continue;
    }
    const float tofNs = static_cast<float>(roundTripHalfNs) * 0.25f;
    const float distance =
        tofNs * (kSpeedOfLightMetersPerSecond / 1000000000.0f);
    if (!isfinite(distance) || distance <= 0.0f) {
      continue;
    }
    rttDistances[rttCount++] = distance;
  }

  if (rttCount < kMinimumControllerRttPairs) {
    return false;
  }

  float median = 0.0f;
  float mad = 0.0f;
  if (!estimateMedianAndMadWide(rttDistances, rttCount, &median, &mad)) {
    return false;
  }

  float varianceAccum = 0.0f;
  size_t inlierCount = 0U;
  const float threshold = fmaxf(0.20f, mad * 3.0f);
  for (size_t i = 0U; i < rttCount; ++i) {
    if (fabsf(rttDistances[i] - median) > threshold) {
      continue;
    }
    const float err = rttDistances[i] - median;
    varianceAccum += err * err;
    ++inlierCount;
  }
  if (inlierCount < kMinimumControllerRttPairs) {
    return false;
  }

  outEstimate->rttChannels = static_cast<uint8_t>(
      (inlierCount <= 255U) ? inlierCount : 255U);
  outEstimate->rttDistanceMeters = median;
  outEstimate->rttVariance = varianceAccum / static_cast<float>(inlierCount);
  return true;
}

bool parseControllerStepBufferCallback(const BleCsSubeventStep* step, void* userData) {
  BleCsControllerBufferParseContext* context =
      static_cast<BleCsControllerBufferParseContext*>(userData);
  if (context == nullptr || step == nullptr) {
    return false;
  }

  if (step->mode > kBleCsMainMode3) {
    return false;
  }
  if (!validCsChannelIndex(step->channel)) {
    return false;
  }
  if (step->mode == kBleCsMainMode0) {
    const uint8_t expectedLength = context->bufferRoleIsInitiator ? 5U : 3U;
    return step->dataLen == expectedLength;
  }

  if (step->mode == kBleCsMainMode2 || step->mode == kBleCsMainMode3) {
    uint8_t toneCount = 0U;
    if (step->mode == kBleCsMainMode2) {
      BleCsStepMode2Data mode2{};
      if (!BleChannelSoundingRadio::parseMode2StepData(step, &mode2)) {
        return false;
      }
      toneCount = mode2.toneCount;
    } else {
      BleCsStepMode3Data mode3{};
      if (!BleChannelSoundingRadio::parseMode3StepData(step, &mode3)) {
        return false;
      }
      toneCount = mode3.toneCount;
    }

    for (uint8_t toneIndex = 0U; toneIndex < toneCount; ++toneIndex) {
      BleCsStepToneInfo tone{};
      const bool parsed =
          (step->mode == kBleCsMainMode2)
              ? BleChannelSoundingRadio::parseMode2ToneInfo(step, toneIndex, &tone)
              : BleChannelSoundingRadio::parseMode3ToneInfo(step, toneIndex, &tone);
      if (!parsed || tone.extensionIndicator != kBleCsToneExtensionNone) {
        continue;
      }

      if (!context->fillingPeer) {
        if (context->phasePairs == nullptr || context->phaseCount == nullptr ||
            *context->phaseCount >= context->phaseCapacity) {
          return false;
        }
        BleCsControllerPhasePair& pair = context->phasePairs[*context->phaseCount];
        pair.failed = (tone.qualityIndicator == kBleCsToneQualityLow) ||
                      (tone.qualityIndicator == kBleCsToneQualityUnavailable);
        pair.localPresent = true;
        pair.channel = step->channel;
        pair.local = tone.pct;
        pair.localQuality = tone.qualityIndicator;
        ++(*context->phaseCount);
      } else {
        if (context->phasePairs == nullptr || context->phaseCount == nullptr ||
            context->phaseCursor >= *context->phaseCount) {
          return false;
        }
        BleCsControllerPhasePair& pair = context->phasePairs[context->phaseCursor++];
        if (pair.channel != step->channel) {
          return false;
        }
        if ((tone.qualityIndicator == kBleCsToneQualityLow) ||
            (tone.qualityIndicator == kBleCsToneQualityUnavailable)) {
          pair.failed = true;
        }
        pair.peerPresent = true;
        pair.peer = tone.pct;
        pair.peerQuality = tone.qualityIndicator;
      }
    }
  }

  if (step->mode == kBleCsMainMode1 || step->mode == kBleCsMainMode3) {
    BleCsStepMode1Data mode1{};
    if (!BleChannelSoundingRadio::parseMode1StepData(step, &mode1)) {
      return false;
    }
    const bool invalidRtt =
        (mode1.aaCheckQuality != kBleCsPacketQualityAaCheckOk) ||
        (static_cast<uint8_t>(mode1.packetRssiDbm) == kBleCsPacketRssiNotAvailable) ||
        (mode1.timeDifferenceHalfNs == kBleCsTimeDifferenceNotAvailable);

    if (!context->fillingPeer) {
      if (context->rttPairs == nullptr || context->rttCount == nullptr ||
          *context->rttCount >= context->rttCapacity) {
        return false;
      }
      BleCsControllerRttPair& pair = context->rttPairs[*context->rttCount];
      pair.failed = invalidRtt;
      pair.channel = step->channel;
      if (context->bufferRoleIsInitiator) {
        pair.initiatorPresent = true;
        pair.toaTodInitiator = mode1.timeDifferenceHalfNs;
      } else {
        pair.reflectorPresent = true;
        pair.todToaReflector = mode1.timeDifferenceHalfNs;
      }
      ++(*context->rttCount);
    } else {
      if (context->rttPairs == nullptr || context->rttCount == nullptr ||
          context->rttCursor >= *context->rttCount) {
        return false;
      }
      BleCsControllerRttPair& pair = context->rttPairs[context->rttCursor++];
      if (pair.channel != step->channel) {
        return false;
      }
      if (invalidRtt) {
        pair.failed = true;
      }
      if (context->bufferRoleIsInitiator) {
        pair.initiatorPresent = true;
        pair.toaTodInitiator = mode1.timeDifferenceHalfNs;
      } else {
        pair.reflectorPresent = true;
        pair.todToaReflector = mode1.timeDifferenceHalfNs;
      }
    }
  }

  return true;
}

bool parseAbortReasonNibble(uint8_t packed,
                            uint8_t* outProcedureAbortReason,
                            uint8_t* outSubeventAbortReason) {
  if (outProcedureAbortReason == nullptr || outSubeventAbortReason == nullptr) {
    return false;
  }
  *outProcedureAbortReason = static_cast<uint8_t>(packed & 0x0FU);
  *outSubeventAbortReason = static_cast<uint8_t>((packed >> 4U) & 0x0FU);
  return true;
}

uint8_t packAbortReasonNibble(uint8_t procedureAbortReason,
                              uint8_t subeventAbortReason) {
  return static_cast<uint8_t>((procedureAbortReason & 0x0FU) |
                              ((subeventAbortReason & 0x0FU) << 4U));
}

bool advanceSubeventStep(const uint8_t* stepData,
                         size_t stepDataLen,
                         size_t* offset) {
  if (stepData == nullptr || offset == nullptr || *offset > stepDataLen ||
      (stepDataLen - *offset) < kBleCsStepHeaderLen) {
    return false;
  }

  const size_t stepLen =
      kBleCsStepHeaderLen + static_cast<size_t>(stepData[*offset + 2U]);
  if (stepLen == kBleCsStepHeaderLen || (stepDataLen - *offset) < stepLen) {
    return false;
  }

  *offset += stepLen;
  return true;
}

bool subeventStepOffsetIsBoundary(const uint8_t* stepData,
                                  size_t stepDataLen,
                                  size_t targetOffset) {
  if (targetOffset > stepDataLen || (stepDataLen > 0U && stepData == nullptr)) {
    return false;
  }
  if (targetOffset == 0U) {
    return true;
  }

  size_t offset = 0U;
  while (offset < targetOffset) {
    if (!advanceSubeventStep(stepData, stepDataLen, &offset)) {
      return false;
    }
  }
  return offset == targetOffset;
}

bool selectSubeventStepFragment(const uint8_t* stepData,
                                size_t stepDataLen,
                                size_t startOffset,
                                size_t maxStepBytes,
                                size_t* outBytes,
                                uint16_t* outSteps,
                                bool* outMore) {
  if (outBytes == nullptr || outSteps == nullptr || outMore == nullptr ||
      startOffset > stepDataLen ||
      (stepDataLen > 0U && stepData == nullptr) ||
      !subeventStepOffsetIsBoundary(stepData, stepDataLen, startOffset)) {
    return false;
  }

  *outBytes = 0U;
  *outSteps = 0U;
  *outMore = startOffset < stepDataLen;
  if (startOffset == stepDataLen) {
    return true;
  }

  size_t offset = startOffset;
  while (offset < stepDataLen) {
    if ((stepDataLen - offset) < kBleCsStepHeaderLen) {
      return false;
    }
    const size_t stepLen =
        kBleCsStepHeaderLen + static_cast<size_t>(stepData[offset + 2U]);
    if (stepLen == kBleCsStepHeaderLen || (stepDataLen - offset) < stepLen) {
      return false;
    }
    if ((*outBytes + stepLen) > maxStepBytes) {
      break;
    }
    *outBytes += stepLen;
    ++(*outSteps);
    offset += stepLen;
  }

  if (*outSteps == 0U) {
    return false;
  }

  *outMore = offset < stepDataLen;
  return true;
}

bool rawBytesAllZero(const uint8_t* data, uint8_t len) {
  if (data == nullptr || len == 0U) {
    return true;
  }

  for (uint8_t i = 0U; i < len; ++i) {
    if (data[i] != 0U) {
      return false;
    }
  }
  return true;
}

int16_t clampSignedToBits(int16_t value, uint8_t bits) {
  if (bits == 0U || bits >= 15U) {
    return value;
  }

  const int16_t minValue = static_cast<int16_t>(-(1 << (bits - 1U)));
  const int16_t maxValue = static_cast<int16_t>((1 << (bits - 1U)) - 1);
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

uint32_t encodeSignedField(int16_t value,
                           uint8_t bits,
                           uint32_t mask,
                           uint8_t pos) {
  const int16_t clamped = clampSignedToBits(value, bits);
  const uint32_t fieldMask = mask >> pos;
  const uint32_t encoded =
      static_cast<uint32_t>(static_cast<uint16_t>(clamped)) & fieldMask;
  return (encoded << pos) & mask;
}

static const uint8_t kAntennaPathLut2[2][3] = {
    {kAntennaId1, kAntennaId2, kAntennaId2},
    {kAntennaId2, kAntennaId1, kAntennaId1},
};

static const uint8_t kAntennaPathLut3[6][4] = {
    {kAntennaId1, kAntennaId2, kAntennaId3, kAntennaId3},
    {kAntennaId2, kAntennaId1, kAntennaId3, kAntennaId3},
    {kAntennaId1, kAntennaId3, kAntennaId2, kAntennaId2},
    {kAntennaId3, kAntennaId1, kAntennaId2, kAntennaId2},
    {kAntennaId3, kAntennaId2, kAntennaId1, kAntennaId1},
    {kAntennaId2, kAntennaId3, kAntennaId1, kAntennaId1},
};

static const uint8_t kAntennaPathLut4[24][5] = {
    {kAntennaId1, kAntennaId2, kAntennaId3, kAntennaId4, kAntennaId4},
    {kAntennaId2, kAntennaId1, kAntennaId3, kAntennaId4, kAntennaId4},
    {kAntennaId1, kAntennaId3, kAntennaId2, kAntennaId4, kAntennaId4},
    {kAntennaId3, kAntennaId1, kAntennaId2, kAntennaId4, kAntennaId4},
    {kAntennaId3, kAntennaId2, kAntennaId1, kAntennaId4, kAntennaId4},
    {kAntennaId2, kAntennaId3, kAntennaId1, kAntennaId4, kAntennaId4},
    {kAntennaId1, kAntennaId2, kAntennaId4, kAntennaId3, kAntennaId3},
    {kAntennaId2, kAntennaId1, kAntennaId4, kAntennaId3, kAntennaId3},
    {kAntennaId1, kAntennaId4, kAntennaId2, kAntennaId3, kAntennaId3},
    {kAntennaId4, kAntennaId1, kAntennaId2, kAntennaId3, kAntennaId3},
    {kAntennaId4, kAntennaId2, kAntennaId1, kAntennaId3, kAntennaId3},
    {kAntennaId2, kAntennaId4, kAntennaId1, kAntennaId3, kAntennaId3},
    {kAntennaId1, kAntennaId4, kAntennaId3, kAntennaId2, kAntennaId2},
    {kAntennaId4, kAntennaId1, kAntennaId3, kAntennaId2, kAntennaId2},
    {kAntennaId1, kAntennaId3, kAntennaId4, kAntennaId2, kAntennaId2},
    {kAntennaId3, kAntennaId1, kAntennaId4, kAntennaId2, kAntennaId2},
    {kAntennaId3, kAntennaId4, kAntennaId1, kAntennaId2, kAntennaId2},
    {kAntennaId4, kAntennaId3, kAntennaId1, kAntennaId2, kAntennaId2},
    {kAntennaId4, kAntennaId2, kAntennaId3, kAntennaId1, kAntennaId1},
    {kAntennaId2, kAntennaId4, kAntennaId3, kAntennaId1, kAntennaId1},
    {kAntennaId4, kAntennaId3, kAntennaId2, kAntennaId1, kAntennaId1},
    {kAntennaId3, kAntennaId4, kAntennaId2, kAntennaId1, kAntennaId1},
    {kAntennaId3, kAntennaId2, kAntennaId4, kAntennaId1, kAntennaId1},
    {kAntennaId2, kAntennaId3, kAntennaId4, kAntennaId1, kAntennaId1},
};

}  // namespace

BleChannelSoundingRadio::BleChannelSoundingRadio(uint32_t radioBase)
    : radio_(reinterpret_cast<NRF_RADIO_Type*>(static_cast<uintptr_t>(radioBase))),
      radioOwnershipToken_(0U),
      power_(),
      config_(),
      initialized_(false),
      hfxoOwned_(false),
      txPacket_{0},
      rxPacket_{0},
      dfePacket_{0},
      auxDataWords_{0},
      lastDfePacketAmountBytes_(0U),
      lastDfePacketCurrentAmountBytes_(0U),
      lastDfePacketAllZero_(true),
      lastReflectorStatus_(0U),
      lastReflectorTiming_() {}

BleChannelSoundingRadio::~BleChannelSoundingRadio() {
  if (initialized_ || hfxoOwned_ || radioOwnershipToken_ != 0U) {
    end();
  }
  if (radio_ != nullptr &&
      ((radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos) !=
          RADIO_STATE_STATE_Disabled) {
    power_.quarantineConstantLatencyLease();
  }
  if (radioOwnershipToken_ != 0U) {
    (void)nrf54l15_quarantine_exclusive_radio(
        Nrf54ExclusiveRadioOwner::kRawChannelSounding,
        radioOwnershipToken_);
    nrf54l15_exclusive_radio_fail_stop();
  }
}

bool BleChannelSoundingRadio::begin(const BleCsConfig& config) {
  if (radio_ == nullptr) {
    initialized_ = false;
    return false;
  }
  if (initialized_ || radioOwnershipToken_ != 0U) {
    return false;
  }
  if (!validLogicalChannel(config.controlChannel) || config.maxPayloadLength == 0U ||
      config.cteTimeUnits < 2U ||
      config.cteTimeUnits > 10U ||
      config.dfeSwitchPatternCount > kBleCsMaxSwitchPatternCount) {
    initialized_ = false;
    return false;
  }

  radioOwnershipToken_ = nrf54l15_acquire_exclusive_radio(
      Nrf54ExclusiveRadioOwner::kRawChannelSounding);
  if (radioOwnershipToken_ == 0U) {
    return false;
  }

  config_ = config;

  const bool hfxoWasRunning = channelSoundingHfxoRunning();
  hfxoOwned_ = hfxoOwned_ || !hfxoWasRunning;
  if (!ClockControl::startHfxo(true, 1500000UL)) {
    initialized_ = false;
    if (hfxoOwned_) {
      ClockControl::stopHfxo();
      hfxoOwned_ = false;
    }
    (void)releaseRawCsRadioOwnershipIfDisabled(&radioOwnershipToken_, radio_);
    return false;
  }

  if (!power_.setLatencyMode(PowerLatencyMode::kConstantLatency)) {
    initialized_ = false;
    if (hfxoOwned_) {
      ClockControl::stopHfxo();
      hfxoOwned_ = false;
    }
    (void)releaseRawCsRadioOwnershipIfDisabled(&radioOwnershipToken_, radio_);
    return false;
  }

  if (!configureBle2MCommon()) {
    initialized_ = false;
    if (((radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos) ==
        RADIO_STATE_STATE_Disabled) {
      power_.setLatencyMode(PowerLatencyMode::kLowPower);
      if (hfxoOwned_) {
        ClockControl::stopHfxo();
        hfxoOwned_ = false;
      }
      (void)releaseRawCsRadioOwnershipIfDisabled(&radioOwnershipToken_, radio_);
    }
    return false;
  }

  resetDfeCaptureState();
  initialized_ = true;
  return true;
}

void BleChannelSoundingRadio::end() {
  if (radioOwnershipToken_ == 0U) {
    initialized_ = false;
    return;
  }
  if (!nrf54l15_exclusive_radio_is_owned_by(
          Nrf54ExclusiveRadioOwner::kRawChannelSounding,
          radioOwnershipToken_)) {
    return;
  }
  if (radio_ == nullptr) {
    initialized_ = false;
    power_.setLatencyMode(PowerLatencyMode::kLowPower);
    if (hfxoOwned_) {
      ClockControl::stopHfxo();
      hfxoOwned_ = false;
    }
    (void)releaseRawCsRadioOwnershipIfDisabled(&radioOwnershipToken_, radio_);
    return;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled = waitForRadioDisabled(radio_, kRadioDisableBudgetUs);
  initialized_ = false;
  if (disabled) {
    detachRawRadioAutomation(radio_);
    clearEvents();
    power_.setLatencyMode(PowerLatencyMode::kLowPower);
    if (hfxoOwned_) {
      ClockControl::stopHfxo();
      hfxoOwned_ = false;
    }
    (void)releaseRawCsRadioOwnershipIfDisabled(&radioOwnershipToken_, radio_);
  }
}

bool BleChannelSoundingRadio::initialized() const { return initialized_; }

const BleCsConfig& BleChannelSoundingRadio::config() const { return config_; }

uint8_t BleChannelSoundingRadio::lastReflectorStatus() const {
  return lastReflectorStatus_;
}

BleCsReflectorTiming BleChannelSoundingRadio::lastReflectorTiming() const {
  return lastReflectorTiming_;
}

BleCsIqSample BleChannelSoundingRadio::parsePctSample(const uint8_t pct[3]) {
  if (pct == nullptr) {
    return BleCsIqSample{};
  }

  const uint32_t packed = static_cast<uint32_t>(pct[0]) |
                          (static_cast<uint32_t>(pct[1]) << 8U) |
                          (static_cast<uint32_t>(pct[2]) << 16U);
  const uint16_t iBits = static_cast<uint16_t>(packed & 0x0FFFU);
  const uint16_t qBits = static_cast<uint16_t>((packed >> 12U) & 0x0FFFU);
  const int16_t i = static_cast<int16_t>((iBits ^ (1U << 11U)) - (1U << 11U));
  const int16_t q = static_cast<int16_t>((qBits ^ (1U << 11U)) - (1U << 11U));
  return BleCsIqSample{.i = i, .q = q};
}

void BleChannelSoundingRadio::fillValidChannelMap(uint8_t channelMap[kBleCsChannelMapBytes]) {
  if (channelMap == nullptr) {
    return;
  }

  memset(channelMap, 0xFF, kCsChmapLen);
  channelMapBitSetVal(channelMap, 0U, 0U);
  channelMapBitSetVal(channelMap, 1U, 0U);
  channelMapBitSetVal(channelMap, 23U, 0U);
  channelMapBitSetVal(channelMap, 24U, 0U);
  channelMapBitSetVal(channelMap, 25U, 0U);
  channelMapBitSetVal(channelMap, 77U, 0U);
  channelMapBitSetVal(channelMap, 78U, 0U);
  channelMapBitSetVal(channelMap, 79U, 0U);
}

bool BleChannelSoundingRadio::getAntennaPathPermutation(uint8_t antennaPathCount,
                                                        uint8_t permutationIndex,
                                                        uint8_t toneIndex,
                                                        uint8_t* outAntennaId) {
  if (outAntennaId == nullptr) {
    return false;
  }

  switch (antennaPathCount) {
    case 2U:
      if (permutationIndex >= 2U || toneIndex >= 3U) {
        return false;
      }
      *outAntennaId = kAntennaPathLut2[permutationIndex][toneIndex];
      return true;
    case 3U:
      if (permutationIndex >= 6U || toneIndex >= 4U) {
        return false;
      }
      *outAntennaId = kAntennaPathLut3[permutationIndex][toneIndex];
      return true;
    case 4U:
      if (permutationIndex >= 24U || toneIndex >= 5U) {
        return false;
      }
      *outAntennaId = kAntennaPathLut4[permutationIndex][toneIndex];
      return true;
    default:
      return false;
  }
}

bool BleChannelSoundingRadio::parseMode1StepData(const BleCsSubeventStep* step,
                                                 BleCsStepMode1Data* outData) {
  if (step == nullptr || outData == nullptr || step->data == nullptr ||
      step->dataLen < kBleCsMode1StepLen) {
    return false;
  }
  if (step->mode != kBleCsMainMode1 && step->mode != kBleCsMainMode3) {
    return false;
  }

  bool hasRttSoundingSequence = false;
  if (step->mode == kBleCsMainMode1) {
    if (step->dataLen == kBleCsMode1StepLen) {
      hasRttSoundingSequence = false;
    } else if (step->dataLen == kBleCsMode1SsRttStepLen) {
      hasRttSoundingSequence = true;
    } else {
      return false;
    }
  } else {
    // Mode 3 normal and sounding-sequence layouts differ by eight bytes, so
    // their lengths are indistinguishable modulo one four-byte tone record.
    // The unqualified parser therefore uses the normal layout. Call the
    // explicit parseMode3StepData() overload when RTT SS was configured.
    if (step->dataLen < kBleCsMode3StepBaseLen ||
        ((step->dataLen - kBleCsMode3StepBaseLen) % kBleCsToneInfoLen) != 0U) {
      return false;
    }
  }

  const uint8_t quality = step->data[0];
  outData->aaCheckQuality = static_cast<uint8_t>(quality & 0x0FU);
  outData->bitErrors = static_cast<uint8_t>((quality >> 4U) & 0x0FU);
  outData->nadm = step->data[1];
  outData->packetRssiDbm = static_cast<int8_t>(step->data[2]);
  outData->timeDifferenceHalfNs = readLe16Signed(step->data + 3U);
  outData->packetAntenna = step->data[5];
  outData->hasRttSoundingSequence = hasRttSoundingSequence;
  outData->soundingPct1 = BleCsIqSample{};
  outData->soundingPct2 = BleCsIqSample{};
  if (hasRttSoundingSequence) {
    outData->soundingPct1 = parsePctSample(step->data + 6U);
    outData->soundingPct2 = parsePctSample(step->data + 10U);
  }
  return true;
}

bool BleChannelSoundingRadio::parseMode2StepData(const BleCsSubeventStep* step,
                                                 BleCsStepMode2Data* outData) {
  if (step == nullptr || outData == nullptr || step->data == nullptr ||
      step->dataLen < 1U || step->mode != kBleCsMainMode2) {
    return false;
  }
  const size_t toneBytes = static_cast<size_t>(step->dataLen - 1U);
  if (toneBytes == 0U || (toneBytes % kBleCsToneInfoLen) != 0U) {
    return false;
  }

  outData->antennaPermutationIndex = step->data[0];
  outData->toneCount = static_cast<uint8_t>(toneBytes / kBleCsToneInfoLen);
  return true;
}

bool BleChannelSoundingRadio::parseMode3StepData(const BleCsSubeventStep* step,
                                                 BleCsStepMode3Data* outData) {
  return parseMode3StepData(step, false, outData);
}

bool BleChannelSoundingRadio::parseMode3StepData(
    const BleCsSubeventStep* step,
    bool hasRttSoundingSequence,
    BleCsStepMode3Data* outData) {
  if (step == nullptr || outData == nullptr || step->data == nullptr ||
      step->mode != kBleCsMainMode3 || step->dataLen < kBleCsMode3StepBaseLen) {
    return false;
  }

  if (!parseMode1StepData(step, &outData->timing)) {
    return false;
  }
  const uint8_t toneDataOffset =
      hasRttSoundingSequence
          ? static_cast<uint8_t>(kBleCsMode3SsRttStepBaseLen)
          : static_cast<uint8_t>(kBleCsMode3StepBaseLen);
  if (step->dataLen < toneDataOffset ||
      ((step->dataLen - toneDataOffset) % kBleCsToneInfoLen) != 0U) {
    return false;
  }
  const uint8_t toneCount = static_cast<uint8_t>(
      (step->dataLen - toneDataOffset) / kBleCsToneInfoLen);
  if (toneCount == 0U) {
    return false;
  }
  outData->timing.hasRttSoundingSequence = hasRttSoundingSequence;
  outData->timing.soundingPct1 = BleCsIqSample{};
  outData->timing.soundingPct2 = BleCsIqSample{};
  if (hasRttSoundingSequence) {
    outData->timing.soundingPct1 = parsePctSample(step->data + 6U);
    outData->timing.soundingPct2 = parsePctSample(step->data + 10U);
  }
  outData->antennaPermutationIndex = step->data[toneDataOffset - 1U];
  outData->toneCount = toneCount;
  outData->toneDataOffset = toneDataOffset;
  return true;
}

bool BleChannelSoundingRadio::parseToneInfo(const uint8_t* toneData,
                                            size_t toneDataLen,
                                            BleCsStepToneInfo* outInfo) {
  if (toneData == nullptr || outInfo == nullptr || toneDataLen < kBleCsToneInfoLen) {
    return false;
  }

  outInfo->pct = parsePctSample(toneData);
  const uint8_t info = toneData[3];
  outInfo->qualityIndicator = static_cast<uint8_t>(info & 0x0FU);
  outInfo->extensionIndicator = static_cast<uint8_t>((info >> 4U) & 0x0FU);
  return true;
}

bool BleChannelSoundingRadio::parseMode2ToneInfo(const BleCsSubeventStep* step,
                                                 uint8_t toneIndex,
                                                 BleCsStepToneInfo* outInfo) {
  BleCsStepMode2Data mode2{};
  if (!parseMode2StepData(step, &mode2) || outInfo == nullptr ||
      toneIndex >= mode2.toneCount) {
    return false;
  }

  const size_t offset = 1U + (static_cast<size_t>(toneIndex) * kBleCsToneInfoLen);
  return parseToneInfo(step->data + offset, static_cast<size_t>(step->dataLen) - offset,
                       outInfo);
}

bool BleChannelSoundingRadio::parseMode3ToneInfo(const BleCsSubeventStep* step,
                                                 uint8_t toneIndex,
                                                 BleCsStepToneInfo* outInfo) {
  return parseMode3ToneInfo(step, false, toneIndex, outInfo);
}

bool BleChannelSoundingRadio::parseMode3ToneInfo(
    const BleCsSubeventStep* step,
    bool hasRttSoundingSequence,
    uint8_t toneIndex,
    BleCsStepToneInfo* outInfo) {
  BleCsStepMode3Data mode3{};
  if (!parseMode3StepData(step, hasRttSoundingSequence, &mode3) ||
      outInfo == nullptr ||
      toneIndex >= mode3.toneCount) {
    return false;
  }

  const size_t offset =
      static_cast<size_t>(mode3.toneDataOffset) +
      (static_cast<size_t>(toneIndex) * kBleCsToneInfoLen);
  return parseToneInfo(step->data + offset, static_cast<size_t>(step->dataLen) - offset,
                       outInfo);
}

bool BleChannelSoundingRadio::parseSubeventStepData(
    const uint8_t* stepData,
    size_t stepDataLen,
    bool (*callback)(const BleCsSubeventStep* step, void* userData),
    void* userData) {
  if (stepData == nullptr || callback == nullptr) {
    return false;
  }

  size_t offset = 0U;
  while ((stepDataLen - offset) >= kBleCsStepHeaderLen) {
    BleCsSubeventStep step{};
    step.mode = stepData[offset + 0U];
    step.channel = stepData[offset + 1U];
    step.dataLen = stepData[offset + 2U];
    offset += kBleCsStepHeaderLen;

    if (step.dataLen == 0U) {
      return false;
    }
    if ((stepDataLen - offset) < step.dataLen) {
      return false;
    }

    step.data = stepData + offset;
    if (!callback(&step, userData)) {
      return false;
    }
    offset += step.dataLen;
  }
  return offset == stepDataLen;
}

bool BleChannelSoundingRadio::encodeMode2StepDataFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    bool peerSide,
    uint8_t* outStepData,
    size_t maxStepDataLen,
    size_t* outStepDataLen,
    uint16_t* outStepsEncoded) {
  if (measurements == nullptr || outStepData == nullptr || outStepDataLen == nullptr ||
      outStepsEncoded == nullptr) {
    return false;
  }

  *outStepDataLen = 0U;
  *outStepsEncoded = 0U;

  size_t offset = 0U;
  for (size_t i = 0U; i < count; ++i) {
    const BleCsChannelMeasurement& measurement = measurements[i];
    if (!measurement.valid || !validDataChannel(measurement.channelIndex)) {
      continue;
    }

    const BleCsToneSample& tone =
        peerSide ? measurement.peerTone : measurement.localTone;
    if (!tone.valid) {
      continue;
    }

    const uint8_t csChannelIndex = static_cast<uint8_t>(
        logicalChannelToFrequency(measurement.channelIndex) - 2U);
    if (!appendMode2ToneStep(outStepData, maxStepDataLen, &offset,
                             csChannelIndex, 0U, tone)) {
      return false;
    }
    ++(*outStepsEncoded);
  }

  *outStepDataLen = offset;
  return (*outStepsEncoded > 0U);
}

bool BleChannelSoundingRadio::buildMode2SubeventResultFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    bool peerSide,
    const BleCsSubeventResultHeader& headerTemplate,
    uint8_t* outStepData,
    size_t maxStepDataLen,
    BleCsSubeventResult* outResult) {
  if (outResult == nullptr) {
    return false;
  }

  size_t stepDataLen = 0U;
  uint16_t stepsEncoded = 0U;
  if (!encodeMode2StepDataFromMeasurements(measurements, count, peerSide,
                                           outStepData, maxStepDataLen,
                                           &stepDataLen, &stepsEncoded)) {
    *outResult = BleCsSubeventResult{};
    return false;
  }

  BleCsSubeventResult result{};
  result.header = headerTemplate;
  result.header.procedureDoneStatus = kBleCsProcedureDoneComplete;
  result.header.subeventDoneStatus = kBleCsSubeventDoneComplete;
  result.header.procedureAbortReason = 0U;
  result.header.subeventAbortReason = 0U;
  result.header.numAntennaPaths =
      (result.header.numAntennaPaths != 0U) ? result.header.numAntennaPaths : 1U;
  result.header.numStepsReported = stepsEncoded;
  result.stepData = outStepData;
  result.stepDataLen = static_cast<uint16_t>(stepDataLen);
  result.isPartial = false;
  result.isComplete = true;
  result.isContinuation = false;
  *outResult = result;
  return true;
}

bool BleChannelSoundingRadio::parseHciSubeventResultEvent(
    const uint8_t* eventData, size_t eventLen, BleCsSubeventResult* outResult) {
  if (eventData == nullptr || outResult == nullptr ||
      eventLen < kBleCsHciSubeventResultHeaderLen) {
    return false;
  }

  BleCsSubeventResult result{};
  result.header.connHandle = readLe16(eventData + 0U);
  result.header.configId = eventData[2U];
  result.header.startAclConnEventCounter = readLe16(eventData + 3U);
  result.header.procedureCounter = readLe16(eventData + 5U);
  result.header.frequencyCompensation = readLe16(eventData + 7U);
  result.header.referencePowerLevelDbm = static_cast<int8_t>(eventData[9U]);
  result.header.procedureDoneStatus = eventData[10U];
  result.header.subeventDoneStatus = eventData[11U];
  if (!parseAbortReasonNibble(eventData[12U], &result.header.procedureAbortReason,
                              &result.header.subeventAbortReason)) {
    return false;
  }
  result.header.numAntennaPaths = eventData[13U];
  result.header.numStepsReported = eventData[14U];
  result.stepData = eventData + kBleCsHciSubeventResultHeaderLen;
  result.stepDataLen =
      static_cast<uint16_t>(eventLen - kBleCsHciSubeventResultHeaderLen);
  result.isPartial =
      (result.header.subeventDoneStatus == kBleCsSubeventDonePartial);
  result.isComplete = !result.isPartial;
  result.isContinuation = false;

  if (result.header.numStepsReported == 0U) {
    result.stepDataLen = 0U;
    result.stepData = nullptr;
  }

  *outResult = result;
  return true;
}

bool BleChannelSoundingRadio::parseHciSubeventResultContinueEvent(
    const uint8_t* eventData, size_t eventLen, BleCsSubeventResult* outResult) {
  if (eventData == nullptr || outResult == nullptr ||
      eventLen < kBleCsHciSubeventResultContinueHeaderLen) {
    return false;
  }

  BleCsSubeventResult result{};
  result.header.connHandle = readLe16(eventData + 0U);
  result.header.configId = eventData[2U];
  result.header.procedureDoneStatus = eventData[3U];
  result.header.subeventDoneStatus = eventData[4U];
  if (!parseAbortReasonNibble(eventData[5U], &result.header.procedureAbortReason,
                              &result.header.subeventAbortReason)) {
    return false;
  }
  result.header.numAntennaPaths = eventData[6U];
  result.header.numStepsReported = eventData[7U];
  result.stepData = eventData + kBleCsHciSubeventResultContinueHeaderLen;
  result.stepDataLen =
      static_cast<uint16_t>(eventLen - kBleCsHciSubeventResultContinueHeaderLen);
  result.isPartial =
      (result.header.subeventDoneStatus == kBleCsSubeventDonePartial);
  result.isComplete = !result.isPartial;
  result.isContinuation = true;

  if (result.header.numStepsReported == 0U) {
    result.stepDataLen = 0U;
    result.stepData = nullptr;
  }

  *outResult = result;
  return true;
}

bool BleChannelSoundingRadio::buildHciSubeventResultEvent(
    const BleCsSubeventResult& result,
    uint8_t* outEventData,
    size_t maxEventLen,
    size_t* outEventLen) {
  if (outEventData == nullptr || outEventLen == nullptr ||
      result.header.numStepsReported > 0xFFU ||
      maxEventLen < (kBleCsHciSubeventResultHeaderLen + result.stepDataLen) ||
      (result.stepDataLen > 0U && result.stepData == nullptr)) {
    return false;
  }

  const size_t eventLen = kBleCsHciSubeventResultHeaderLen + result.stepDataLen;
  writeLe16(outEventData + 0U, result.header.connHandle);
  outEventData[2U] = result.header.configId;
  writeLe16(outEventData + 3U, result.header.startAclConnEventCounter);
  writeLe16(outEventData + 5U, result.header.procedureCounter);
  writeLe16(outEventData + 7U, result.header.frequencyCompensation);
  outEventData[9U] = static_cast<uint8_t>(result.header.referencePowerLevelDbm);
  outEventData[10U] = result.header.procedureDoneStatus;
  outEventData[11U] = result.header.subeventDoneStatus;
  outEventData[12U] =
      packAbortReasonNibble(result.header.procedureAbortReason,
                            result.header.subeventAbortReason);
  outEventData[13U] =
      (result.header.numAntennaPaths != 0U) ? result.header.numAntennaPaths : 1U;
  outEventData[14U] = static_cast<uint8_t>(result.header.numStepsReported);
  if (result.stepDataLen > 0U) {
    memcpy(outEventData + kBleCsHciSubeventResultHeaderLen,
           result.stepData, result.stepDataLen);
  }
  *outEventLen = eventLen;
  return true;
}

bool BleChannelSoundingRadio::buildHciSubeventResultContinueEvent(
    const BleCsSubeventResult& result,
    uint8_t* outEventData,
    size_t maxEventLen,
    size_t* outEventLen) {
  if (outEventData == nullptr || outEventLen == nullptr ||
      result.header.numStepsReported > 0xFFU ||
      maxEventLen < (kBleCsHciSubeventResultContinueHeaderLen + result.stepDataLen) ||
      (result.stepDataLen > 0U && result.stepData == nullptr)) {
    return false;
  }

  const size_t eventLen =
      kBleCsHciSubeventResultContinueHeaderLen + result.stepDataLen;
  writeLe16(outEventData + 0U, result.header.connHandle);
  outEventData[2U] = result.header.configId;
  outEventData[3U] = result.header.procedureDoneStatus;
  outEventData[4U] = result.header.subeventDoneStatus;
  outEventData[5U] =
      packAbortReasonNibble(result.header.procedureAbortReason,
                            result.header.subeventAbortReason);
  outEventData[6U] =
      (result.header.numAntennaPaths != 0U) ? result.header.numAntennaPaths : 1U;
  outEventData[7U] = static_cast<uint8_t>(result.header.numStepsReported);
  if (result.stepDataLen > 0U) {
    memcpy(outEventData + kBleCsHciSubeventResultContinueHeaderLen,
           result.stepData, result.stepDataLen);
  }
  *outEventLen = eventLen;
  return true;
}

bool BleChannelSoundingRadio::buildH4LeMetaSubeventResultPacket(
    const BleCsSubeventResult& result,
    uint8_t* outPacket,
    size_t maxPacketLen,
    size_t* outPacketLen) {
  if (outPacket == nullptr || outPacketLen == nullptr) {
    return false;
  }
  *outPacketLen = 0U;

  uint8_t payload[255] = {0};
  size_t payloadLen = 0U;
  const uint8_t subeventCode =
      result.isContinuation ? kBleCsHciEvtSubeventResultContinue
                            : kBleCsHciEvtSubeventResult;
  const bool payloadOk =
      result.isContinuation
          ? buildHciSubeventResultContinueEvent(result, payload,
                                                sizeof(payload), &payloadLen)
          : buildHciSubeventResultEvent(result, payload,
                                        sizeof(payload), &payloadLen);
  if (!payloadOk || payloadLen > 254U ||
      !buildH4LeMetaEvent(outPacket, maxPacketLen, subeventCode,
                          payload, payloadLen)) {
    return false;
  }

  *outPacketLen = 4U + payloadLen;
  return true;
}

bool BleChannelSoundingRadio::buildH4LeMetaSubeventResultFragmentPacket(
    const BleCsSubeventResult& result,
    size_t stepDataOffset,
    uint8_t* outPacket,
    size_t maxPacketLen,
    size_t* outPacketLen,
    BleCsSubeventResultFragment* outFragment) {
  if (outPacket == nullptr || outPacketLen == nullptr || outFragment == nullptr ||
      stepDataOffset > result.stepDataLen ||
      (result.stepDataLen > 0U && result.stepData == nullptr)) {
    return false;
  }
  *outPacketLen = 0U;
  *outFragment = BleCsSubeventResultFragment{};

  const bool continuation = stepDataOffset > 0U;
  const size_t headerLen = continuation
                               ? kBleCsHciSubeventResultContinueHeaderLen
                               : kBleCsHciSubeventResultHeaderLen;
  if (headerLen >= kBleCsHciLeMetaMaxPayloadLen ||
      maxPacketLen < (4U + headerLen)) {
    return false;
  }

  const size_t hciStepCapacity = kBleCsHciLeMetaMaxPayloadLen - headerLen;
  const size_t callerStepCapacity = maxPacketLen - 4U - headerLen;
  const size_t maxStepBytes =
      (callerStepCapacity < hciStepCapacity) ? callerStepCapacity : hciStepCapacity;

  size_t fragmentBytes = 0U;
  uint16_t fragmentSteps = 0U;
  bool more = false;
  if (!selectSubeventStepFragment(result.stepData, result.stepDataLen, stepDataOffset,
                                  maxStepBytes, &fragmentBytes, &fragmentSteps,
                                  &more)) {
    return false;
  }

  BleCsSubeventResult fragment = result;
  fragment.stepData = (fragmentBytes > 0U) ? (result.stepData + stepDataOffset) : nullptr;
  fragment.stepDataLen = static_cast<uint16_t>(fragmentBytes);
  fragment.header.numStepsReported = fragmentSteps;
  fragment.isContinuation = continuation;
  fragment.isPartial = more;
  fragment.isComplete = !more;

  if (more) {
    fragment.header.procedureDoneStatus = kBleCsProcedureDonePartial;
    fragment.header.subeventDoneStatus = kBleCsSubeventDonePartial;
    fragment.header.procedureAbortReason = 0U;
    fragment.header.subeventAbortReason = 0U;
  }

  if (!buildH4LeMetaSubeventResultPacket(fragment, outPacket, maxPacketLen,
                                         outPacketLen)) {
    return false;
  }

  outFragment->nextStepDataOffset = stepDataOffset + fragmentBytes;
  outFragment->stepsIncluded = fragmentSteps;
  outFragment->more = more;
  outFragment->continuation = continuation;
  return true;
}

bool BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
    const uint8_t* localStepData, size_t localStepDataLen, const uint8_t* peerStepData,
    size_t peerStepDataLen, bool localRoleIsInitiator, BleCsEstimate* outEstimate) {
  if (localStepData == nullptr || peerStepData == nullptr || outEstimate == nullptr) {
    return false;
  }

  *outEstimate = BleCsEstimate{};
  outEstimate->phaseSlopeDistanceMeters = NAN;
  outEstimate->rttDistanceMeters = NAN;
  outEstimate->distanceMeters = NAN;

  BleCsControllerPhasePair phasePairs[kMaxCsControllerStepSamples] = {};
  BleCsControllerRttPair rttPairs[kMaxCsControllerStepSamples] = {};
  size_t phaseCount = 0U;
  size_t rttCount = 0U;

  BleCsControllerBufferParseContext localContext{};
  localContext.phasePairs = phasePairs;
  localContext.phaseCapacity = kMaxCsControllerStepSamples;
  localContext.phaseCount = &phaseCount;
  localContext.rttPairs = rttPairs;
  localContext.rttCapacity = kMaxCsControllerStepSamples;
  localContext.rttCount = &rttCount;
  localContext.fillingPeer = false;
  localContext.bufferRoleIsInitiator = localRoleIsInitiator;
  if (!parseSubeventStepData(localStepData, localStepDataLen,
                             parseControllerStepBufferCallback,
                             &localContext)) {
    return false;
  }

  BleCsControllerBufferParseContext peerContext{};
  peerContext.phasePairs = phasePairs;
  peerContext.phaseCapacity = kMaxCsControllerStepSamples;
  peerContext.phaseCount = &phaseCount;
  peerContext.rttPairs = rttPairs;
  peerContext.rttCapacity = kMaxCsControllerStepSamples;
  peerContext.rttCount = &rttCount;
  peerContext.fillingPeer = true;
  peerContext.bufferRoleIsInitiator = !localRoleIsInitiator;
  if (!parseSubeventStepData(peerStepData, peerStepDataLen,
                             parseControllerStepBufferCallback,
                             &peerContext) ||
      peerContext.phaseCursor != phaseCount ||
      peerContext.rttCursor != rttCount) {
    return false;
  }

  const bool phaseOk = controllerPhaseDistanceEstimate(phasePairs, phaseCount, outEstimate);
  const bool rttOk = controllerRttDistanceEstimate(rttPairs, rttCount, outEstimate);
  if (!phaseOk && !rttOk) {
    return false;
  }

  outEstimate->valid = true;
  if (rttOk && phaseOk) {
    const float delta =
        fabsf(outEstimate->rttDistanceMeters - outEstimate->phaseSlopeDistanceMeters);
    if (delta <= fmaxf(0.25f, sqrtf(outEstimate->rttVariance) + 0.20f)) {
      outEstimate->distanceMeters =
          (0.65f * outEstimate->phaseSlopeDistanceMeters) +
          (0.35f * outEstimate->rttDistanceMeters);
    } else {
      // AA-only RTT carries a board/controller timing bias until calibrated.
      // A coherent multi-channel PBR slope is the primary ranging result.
      outEstimate->distanceMeters = outEstimate->phaseSlopeDistanceMeters;
    }
  } else if (rttOk) {
    outEstimate->distanceMeters = outEstimate->rttDistanceMeters;
  } else {
    outEstimate->distanceMeters = outEstimate->phaseSlopeDistanceMeters;
  }

  return isfinite(outEstimate->distanceMeters) &&
         (outEstimate->distanceMeters > 0.0f);
}

bool BleChannelSoundingRadio::estimateDistanceFromSubeventResults(
    const BleCsSubeventResult& localResult, const BleCsSubeventResult& peerResult,
    bool localRoleIsInitiator, BleCsEstimate* outEstimate) {
  if (!localResult.isComplete || !peerResult.isComplete ||
      localResult.stepData == nullptr || peerResult.stepData == nullptr ||
      localResult.header.numAntennaPaths > 1U ||
      peerResult.header.numAntennaPaths > 1U ||
      (localResult.header.numAntennaPaths != 0U &&
       peerResult.header.numAntennaPaths != 0U &&
       localResult.header.numAntennaPaths !=
           peerResult.header.numAntennaPaths)) {
    return false;
  }

  return estimateDistanceFromStepBuffers(localResult.stepData, localResult.stepDataLen,
                                         peerResult.stepData, peerResult.stepDataLen,
                                         localRoleIsInitiator, outEstimate);
}

float BleChannelSoundingRadio::applyCalibrationProfile(
    float meters, const BleCsCalibrationProfile& profile) {
  if (!isfinite(meters)) {
    return NAN;
  }

  const float calibrated = (meters * profile.scale) + profile.offsetMeters;
  return (calibrated >= 0.0f) ? calibrated : 0.0f;
}

bool BleChannelSoundingRadio::estimatePhysicalDistance(
    float meters, const BleCsCalibrationProfile& profile,
    BleCsPhysicalDistanceEstimate* outEstimate) {
  if (outEstimate == nullptr) {
    return false;
  }
  *outEstimate = BleCsPhysicalDistanceEstimate{};

  const float calibrated = applyCalibrationProfile(meters, profile);
  if (!isfinite(calibrated) || profile.validatedSampleCount == 0U) {
    return false;
  }

  float typicalError = profile.validatedMadMeters;
  if (!(isfinite(typicalError) && typicalError > 0.0f)) {
    typicalError = 0.0f;
  }
  float conservativeError = profile.validatedP90AbsErrorMeters;
  if (!(isfinite(conservativeError) && conservativeError > 0.0f)) {
    conservativeError = typicalError;
  }
  if (!(isfinite(conservativeError) && conservativeError > 0.0f)) {
    return false;
  }
  if (isfinite(profile.referenceDistanceMeters) && profile.referenceDistanceMeters > 0.0f) {
    const float referenceLower =
        profile.referenceDistanceMeters - conservativeError;
    const float referenceUpper =
        profile.referenceDistanceMeters + conservativeError;
    if (calibrated < ((referenceLower >= 0.0f) ? referenceLower : 0.0f) ||
        calibrated > referenceUpper) {
      return false;
    }
  }

  outEstimate->valid = true;
  outEstimate->distanceMeters = calibrated;
  outEstimate->typicalErrorMeters = typicalError;
  outEstimate->conservativeErrorMeters = conservativeError;
  const float lowerBound = calibrated - conservativeError;
  outEstimate->lowerBoundMeters = (lowerBound >= 0.0f) ? lowerBound : 0.0f;
  outEstimate->upperBoundMeters = calibrated + conservativeError;
  outEstimate->sampleCount = profile.validatedSampleCount;
  return true;
}

float BleChannelSoundingRadio::distanceMetersToEquivalentDelayNs(float meters) {
  static constexpr float kSpeedOfLightMetersPerSecond = 299792458.0f;
  return (meters / kSpeedOfLightMetersPerSecond) * 1.0e9f;
}

float BleChannelSoundingRadio::equivalentDelayNsToDistanceMeters(float delayNs) {
  static constexpr float kSpeedOfLightMetersPerSecond = 299792458.0f;
  return (delayNs * 1.0e-9f) * kSpeedOfLightMetersPerSecond;
}

bool BleChannelSoundingRadio::buildHciReadRemoteSupportedCapabilitiesCommand(
    uint16_t connHandle, BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpReadRemoteSupportedCapabilities;
  outCommand->payloadLen = 2U;
  writeLe16(outCommand->payload, connHandle);
  return true;
}

bool BleChannelSoundingRadio::buildHciSetDefaultSettingsCommand(
    uint16_t connHandle, const BleCsDefaultSettings& settings, BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpSetDefaultSettings;
  outCommand->payloadLen = 5U;
  writeLe16(outCommand->payload, connHandle);
  uint8_t roleEnable = 0U;
  if (settings.enableInitiatorRole) {
    roleEnable |= 0x01U;
  }
  if (settings.enableReflectorRole) {
    roleEnable |= 0x02U;
  }
  outCommand->payload[2U] = roleEnable;
  outCommand->payload[3U] = settings.csSyncAntennaSelection;
  outCommand->payload[4U] = static_cast<uint8_t>(settings.maxTxPowerDbm);
  return true;
}

bool BleChannelSoundingRadio::buildHciCreateConfigCommand(
    uint16_t connHandle, const BleCsControllerCreateConfig& config, BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpCreateConfig;
  outCommand->payloadLen = 22U;
  writeLe16(outCommand->payload + 0U, connHandle);
  outCommand->payload[2U] = config.configId;
  outCommand->payload[3U] = config.createContext;
  outCommand->payload[4U] = config.mainModeType;
  outCommand->payload[5U] = config.subModeType;
  outCommand->payload[6U] = config.minMainModeSteps;
  outCommand->payload[7U] = config.maxMainModeSteps;
  outCommand->payload[8U] = config.mainModeRepetition;
  outCommand->payload[9U] = config.mode0Steps;
  outCommand->payload[10U] = config.role;
  outCommand->payload[11U] = config.rttType;
  outCommand->payload[12U] = config.csSyncPhy;
  memcpy(outCommand->payload + 13U, config.channelMap, kBleCsChannelMapBytes);
  outCommand->payload[23U] = config.channelMapRepetition;
  outCommand->payload[24U] = config.channelSelectionType;
  outCommand->payload[25U] = config.ch3cShape;
  outCommand->payload[26U] = config.ch3cJump;
  outCommand->payload[27U] = config.csEnhancements1;
  outCommand->payloadLen = 28U;
  return true;
}

bool BleChannelSoundingRadio::buildHciRemoveConfigCommand(uint16_t connHandle,
                                                          uint8_t configId,
                                                          BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpRemoveConfig;
  outCommand->payloadLen = 3U;
  writeLe16(outCommand->payload + 0U, connHandle);
  outCommand->payload[2U] = configId;
  return true;
}

bool BleChannelSoundingRadio::buildHciSecurityEnableCommand(uint16_t connHandle,
                                                            BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpSecurityEnable;
  outCommand->payloadLen = 2U;
  writeLe16(outCommand->payload, connHandle);
  return true;
}

bool BleChannelSoundingRadio::buildHciSetProcedureParametersCommand(
    uint16_t connHandle, const BleCsProcedureParameters& params, BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpSetProcedureParameters;
  outCommand->payloadLen = 23U;
  writeLe16(outCommand->payload + 0U, connHandle);
  outCommand->payload[2U] = params.configId;
  writeLe16(outCommand->payload + 3U, params.maxProcedureLen);
  writeLe16(outCommand->payload + 5U, params.minProcedureInterval);
  writeLe16(outCommand->payload + 7U, params.maxProcedureInterval);
  writeLe16(outCommand->payload + 9U, params.maxProcedureCount);
  writeLe24(outCommand->payload + 11U, params.minSubeventLen);
  writeLe24(outCommand->payload + 14U, params.maxSubeventLen);
  outCommand->payload[17U] = params.toneAntennaConfigSelection;
  outCommand->payload[18U] = params.phy;
  outCommand->payload[19U] = static_cast<uint8_t>(params.txPowerDelta);
  outCommand->payload[20U] = params.preferredPeerAntenna;
  outCommand->payload[21U] = params.snrControlInitiator;
  outCommand->payload[22U] = params.snrControlReflector;
  return true;
}

bool BleChannelSoundingRadio::buildHciProcedureEnableCommand(
    uint16_t connHandle, const BleCsProcedureEnable& params, BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpProcedureEnable;
  outCommand->payloadLen = 4U;
  writeLe16(outCommand->payload + 0U, connHandle);
  outCommand->payload[2U] = params.configId;
  outCommand->payload[3U] = params.enable;
  return true;
}

bool BleChannelSoundingRadio::encodeHciCommandPacket(const BleCsHciCommand& command,
                                                     uint8_t* outPacket,
                                                     size_t maxLen,
                                                     size_t* outLen) {
  if (outLen != nullptr) {
    *outLen = 0U;
  }
  if (outPacket == nullptr || maxLen < (4U + command.payloadLen)) {
    return false;
  }
  outPacket[0U] = kBleHciPacketTypeCommand;
  writeLe16(outPacket + 1U, command.opcode);
  outPacket[3U] = command.payloadLen;
  if (command.payloadLen > 0U) {
    memcpy(outPacket + 4U, command.payload, command.payloadLen);
  }
  if (outLen != nullptr) {
    *outLen = static_cast<size_t>(4U + command.payloadLen);
  }
  return true;
}

bool BleChannelSoundingRadio::parseHciCommandStatusEvent(
    const uint8_t* packet, size_t packetLen, BleCsHciCommandStatusEvent* outEvent) {
  uint8_t eventCode = 0U;
  const uint8_t* params = nullptr;
  size_t paramsLen = 0U;
  if (outEvent == nullptr ||
      !decodeHciEventFrame(packet, packetLen, &eventCode, &params, &paramsLen) ||
      eventCode != kBleHciEvtCommandStatus || paramsLen < 4U) {
    return false;
  }

  BleCsHciCommandStatusEvent evt{};
  evt.status = params[0U];
  evt.numCommandPackets = params[1U];
  evt.opcode = readLe16(params + 2U);
  *outEvent = evt;
  return true;
}

bool BleChannelSoundingRadio::parseHciCommandCompleteEvent(
    const uint8_t* packet, size_t packetLen, BleCsHciCommandCompleteEvent* outEvent) {
  uint8_t eventCode = 0U;
  const uint8_t* params = nullptr;
  size_t paramsLen = 0U;
  if (outEvent == nullptr ||
      !decodeHciEventFrame(packet, packetLen, &eventCode, &params, &paramsLen) ||
      eventCode != kBleHciEvtCommandComplete || paramsLen < 3U) {
    return false;
  }

  BleCsHciCommandCompleteEvent evt{};
  evt.numCommandPackets = params[0U];
  evt.opcode = readLe16(params + 1U);
  evt.returnParams = (paramsLen > 3U) ? (params + 3U) : nullptr;
  evt.returnParamsLen = static_cast<uint8_t>((paramsLen > 3U) ? (paramsLen - 3U) : 0U);
  evt.status = (evt.returnParamsLen > 0U) ? evt.returnParams[0U] : 0U;
  *outEvent = evt;
  return true;
}

bool BleChannelSoundingRadio::parseHciLeMetaEvent(const uint8_t* packet,
                                                  size_t packetLen,
                                                  BleCsHciLeMetaEvent* outEvent) {
  uint8_t eventCode = 0U;
  const uint8_t* params = nullptr;
  size_t paramsLen = 0U;
  if (outEvent == nullptr ||
      !decodeHciEventFrame(packet, packetLen, &eventCode, &params, &paramsLen) ||
      eventCode != kBleHciEvtLeMeta || paramsLen < 1U) {
    return false;
  }

  BleCsHciLeMetaEvent evt{};
  evt.subeventCode = params[0U];
  evt.payload = (paramsLen > 1U) ? (params + 1U) : nullptr;
  evt.payloadLen = static_cast<uint8_t>((paramsLen > 1U) ? (paramsLen - 1U) : 0U);
  *outEvent = evt;
  return true;
}

bool BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteEvent(
    const uint8_t* eventData, size_t eventLen, BleCsControllerCapabilities* outCapabilities) {
  if (eventData == nullptr || outCapabilities == nullptr ||
      eventLen < kBleCsHciReadRemoteCapsCompleteLen) {
    return false;
  }
  BleCsControllerCapabilities caps{};
  caps.isV2 = false;
  caps.status = eventData[0U];
  caps.connHandle = readLe16(eventData + 1U);
  caps.numConfigSupported = eventData[3U];
  caps.maxConsecutiveProceduresSupported = readLe16(eventData + 4U);
  caps.numAntennasSupported = eventData[6U];
  caps.maxAntennaPathsSupported = eventData[7U];
  const uint8_t roles = eventData[8U];
  const uint8_t modes = eventData[9U];
  caps.initiatorSupported = (roles & 0x01U) != 0U;
  caps.reflectorSupported = (roles & 0x02U) != 0U;
  caps.mode3Supported = (modes & 0x01U) != 0U;
  caps.rttCapability = eventData[10U];
  caps.rttAaOnlyN = eventData[11U];
  caps.rttSoundingN = eventData[12U];
  caps.rttRandomPayloadN = eventData[13U];
  caps.nadmSoundingCapability = readLe16(eventData + 14U);
  caps.nadmRandomCapability = readLe16(eventData + 16U);
  const uint8_t syncPhys = eventData[18U];
  caps.csSync2mPhySupported = (syncPhys & 0x02U) != 0U;
  caps.csSync2m2btPhySupported = (syncPhys & 0x04U) != 0U;
  const uint16_t subfeatures = readLe16(eventData + 19U);
  caps.csWithoutFaeSupported = (subfeatures & 0x0002U) != 0U;
  caps.chselAlg3cSupported = (subfeatures & 0x0004U) != 0U;
  caps.pbrFromRttSoundingSeqSupported = (subfeatures & 0x0008U) != 0U;
  caps.tIp1TimesSupported = readLe16(eventData + 21U);
  caps.tIp2TimesSupported = readLe16(eventData + 23U);
  caps.tFcsTimesSupported = readLe16(eventData + 25U);
  caps.tPmTimesSupported = readLe16(eventData + 27U);
  caps.valid = (caps.status == 0U);
  if (eventLen >= 30U) {
    caps.tSwTimeSupported = eventData[29U];
  }
  if (eventLen >= 31U) {
    caps.txSnrCapability = eventData[30U];
  }
  *outCapabilities = caps;
  return true;
}

bool BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteV2Event(
    const uint8_t* eventData, size_t eventLen, BleCsControllerCapabilities* outCapabilities) {
  if (eventData == nullptr || outCapabilities == nullptr ||
      eventLen < kBleCsHciReadRemoteCapsCompleteV2Len) {
    return false;
  }
  BleCsControllerCapabilities caps{};
  if (!parseHciRemoteSupportedCapabilitiesCompleteEvent(eventData, eventLen, &caps)) {
    return false;
  }
  caps.isV2 = true;
  caps.csIptReflectorSupported = (readLe16(eventData + 19U) & 0x0010U) != 0U;
  caps.tSwTimeSupported = eventData[29U];
  caps.txSnrCapability = eventData[30U];
  caps.tIp2IptTimesSupported = readLe16(eventData + 31U);
  if (eventLen >= 34U) {
    caps.tSwIptTimeSupported = eventData[33U];
  }
  *outCapabilities = caps;
  return true;
}

bool BleChannelSoundingRadio::parseHciSecurityEnableCompleteEvent(
    const uint8_t* eventData, size_t eventLen, BleCsSecurityEnableComplete* outEvent) {
  if (eventData == nullptr || outEvent == nullptr ||
      eventLen < kBleCsHciSecurityEnableCompleteLen) {
    return false;
  }
  BleCsSecurityEnableComplete evt{};
  evt.status = eventData[0U];
  evt.connHandle = readLe16(eventData + 1U);
  *outEvent = evt;
  return true;
}

bool BleChannelSoundingRadio::parseHciConfigCompleteEvent(const uint8_t* eventData,
                                                          size_t eventLen,
                                                          BleCsConfigComplete* outEvent) {
  if (eventData == nullptr || outEvent == nullptr ||
      eventLen < kBleCsHciConfigCompleteLen) {
    return false;
  }
  BleCsConfigComplete evt{};
  evt.status = eventData[0U];
  evt.connHandle = readLe16(eventData + 1U);
  evt.configId = eventData[3U];
  evt.action = eventData[4U];
  evt.mainModeType = eventData[5U];
  evt.subModeType = eventData[6U];
  evt.minMainModeSteps = eventData[7U];
  evt.maxMainModeSteps = eventData[8U];
  evt.mainModeRepetition = eventData[9U];
  evt.mode0Steps = eventData[10U];
  evt.role = eventData[11U];
  evt.rttType = eventData[12U];
  evt.csSyncPhy = eventData[13U];
  memcpy(evt.channelMap, eventData + 14U, kBleCsChannelMapBytes);
  evt.channelMapRepetition = eventData[24U];
  evt.channelSelectionType = eventData[25U];
  evt.ch3cShape = eventData[26U];
  evt.ch3cJump = eventData[27U];
  evt.csEnhancements1 = eventData[28U];
  if (eventLen >= 33U) {
    evt.tIp1TimeUs = eventData[29U];
    evt.tIp2TimeUs = eventData[30U];
    evt.tFcsTimeUs = eventData[31U];
    evt.tPmTimeUs = eventData[32U];
  }
  *outEvent = evt;
  return true;
}

bool BleChannelSoundingRadio::parseHciProcedureEnableCompleteEvent(
    const uint8_t* eventData, size_t eventLen, BleCsProcedureEnableComplete* outEvent) {
  if (eventData == nullptr || outEvent == nullptr ||
      eventLen < kBleCsHciProcedureEnableCompleteLen) {
    return false;
  }
  BleCsProcedureEnableComplete evt{};
  evt.status = eventData[0U];
  evt.connHandle = readLe16(eventData + 1U);
  evt.configId = eventData[3U];
  evt.state = eventData[4U];
  evt.toneAntennaConfigSelection = eventData[5U];
  evt.selectedTxPower = static_cast<int8_t>(eventData[6U]);
  evt.subeventLen = readLe24(eventData + 7U);
  evt.subeventsPerEvent = eventData[10U];
  evt.subeventInterval = readLe16(eventData + 11U);
  evt.eventInterval = readLe16(eventData + 13U);
  evt.procedureInterval = readLe16(eventData + 15U);
  if (eventLen >= 19U) {
    evt.procedureCount = readLe16(eventData + 17U);
  }
  if (eventLen >= 21U) {
    evt.maxProcedureLen = readLe16(eventData + 19U);
  }
  *outEvent = evt;
  return true;
}

// ─── Zephyr Parity: CS Test Mode ─────────────────────────────────

bool BleChannelSoundingRadio::buildHciTestCommand(
    const BleCsTestParams& params, BleCsHciCommand* outCommand) {
  if (outCommand == nullptr ||
      params.subeventLenUs < 1250U ||
      params.subeventLenUs > 4000000U ||
      (params.overrideConfig & ~kBleCsTestSupportedOverrideMask) != 0U ||
      ((params.overrideConfig & kBleCsTestOverrideChannelSelection) != 0U &&
       (params.overrideChannelListLen == 0U ||
        params.overrideChannelListLen > kBleCsMaxTestChannelCount))) {
    return false;
  }

  BleCsHciCommand cmd{};
  cmd.opcode = kBleCsHciOpTest;
  size_t offset = 0U;
  const auto appendByte = [&cmd, &offset](uint8_t value) -> bool {
    if (offset >= sizeof(cmd.payload)) {
      return false;
    }
    cmd.payload[offset++] = value;
    return true;
  };
  const auto appendBytes = [&cmd, &offset](const uint8_t* data, size_t len) -> bool {
    if ((data == nullptr && len != 0U) || len > (sizeof(cmd.payload) - offset)) {
      return false;
    }
    if (len != 0U) {
      memcpy(cmd.payload + offset, data, len);
      offset += len;
    }
    return true;
  };
  const auto appendLe16 = [&cmd, &offset](uint16_t value) -> bool {
    if (2U > (sizeof(cmd.payload) - offset)) {
      return false;
    }
    writeLe16(cmd.payload + offset, value);
    offset += 2U;
    return true;
  };
  const auto appendLe32 = [&cmd, &offset](uint32_t value) -> bool {
    if (4U > (sizeof(cmd.payload) - offset)) {
      return false;
    }
    writeLe32(cmd.payload + offset, value);
    offset += 4U;
    return true;
  };

  if (!appendByte(params.mainModeType) ||
      !appendByte(params.subModeType) ||
      !appendByte(params.mainModeRepetition) ||
      !appendByte(params.mode0Steps) ||
      !appendByte(params.role) ||
      !appendByte(params.rttType) ||
      !appendByte(params.csSyncPhy) ||
      !appendByte(params.csSyncAntennaSelection)) {
    return false;
  }
  if (3U > (sizeof(cmd.payload) - offset)) {
    return false;
  }
  writeLe24(cmd.payload + offset, params.subeventLenUs);
  offset += 3U;
  if (!appendLe16(params.subeventInterval) ||
      !appendByte(params.maxNumSubevents) ||
      !appendByte(params.transmitPowerLevel) ||
      !appendByte(params.tIp1TimeUs) ||
      !appendByte(params.tIp2TimeUs) ||
      !appendByte(params.tFcsTimeUs) ||
      !appendByte(params.tPmTimeUs) ||
      !appendByte(params.tSwTimeUs) ||
      !appendByte(params.toneAntennaConfigSelection) ||
      !appendByte(params.csEnhancements1) ||
      !appendByte(params.snrControlInitiator) ||
      !appendByte(params.snrControlReflector) ||
      !appendLe16(params.drbgNonce) ||
      !appendByte(params.channelMapRepetition) ||
      !appendLe16(params.overrideConfig)) {
    return false;
  }

  const size_t overrideLengthOffset = offset;
  if (!appendByte(0U)) {
    return false;
  }
  const size_t overrideStart = offset;

  if ((params.overrideConfig & kBleCsTestOverrideChannelSelection) != 0U) {
    if (!appendByte(params.overrideChannelListLen) ||
        !appendBytes(params.overrideChannelList, params.overrideChannelListLen)) {
      return false;
    }
  } else if (!appendBytes(params.channelMap, kBleCsChannelMapBytes) ||
             !appendByte(params.channelSelectionType) ||
             !appendByte(params.ch3cShape) ||
             !appendByte(params.ch3cJump)) {
    return false;
  }

  if ((params.overrideConfig & kBleCsTestOverrideMainModeSteps) != 0U &&
      !appendByte(params.overrideMainModeSteps)) {
    return false;
  }
  if ((params.overrideConfig & kBleCsTestOverrideToneExtension) != 0U &&
      !appendByte(params.overrideTpmToneExtension)) {
    return false;
  }
  if ((params.overrideConfig & kBleCsTestOverrideAntennaPermutation) != 0U &&
      !appendByte(params.overrideToneAntennaPermutationIndex)) {
    return false;
  }
  if ((params.overrideConfig & kBleCsTestOverrideAccessAddresses) != 0U &&
      (!appendLe32(params.overrideCsSyncAccessAddressInitiator) ||
       !appendLe32(params.overrideCsSyncAccessAddressReflector))) {
    return false;
  }
  if ((params.overrideConfig & kBleCsTestOverrideMarkerPositions) != 0U &&
      !appendBytes(params.overrideSsMarkerPositions,
                   sizeof(params.overrideSsMarkerPositions))) {
    return false;
  }
  if ((params.overrideConfig & kBleCsTestOverrideMarkerValue) != 0U &&
      !appendByte(params.overrideSsMarkerValue)) {
    return false;
  }
  if ((params.overrideConfig & kBleCsTestOverridePayload) != 0U &&
      (!appendByte(params.overrideCsSyncPayloadPattern) ||
       !appendBytes(params.overrideCsSyncUserPayload,
                    sizeof(params.overrideCsSyncUserPayload)))) {
    return false;
  }

  const size_t overrideLength = offset - overrideStart;
  if (overrideLength > 0xFFU || offset > 0xFFU) {
    return false;
  }
  cmd.payload[overrideLengthOffset] = static_cast<uint8_t>(overrideLength);
  cmd.payloadLen = static_cast<uint8_t>(offset);
  *outCommand = cmd;
  return true;
}

bool BleChannelSoundingRadio::buildHciTestCommand(
    uint16_t unusedConnHandle,
    const BleCsTestParams& params,
    BleCsHciCommand* outCommand) {
  (void)unusedConnHandle;
  return buildHciTestCommand(params, outCommand);
}

bool BleChannelSoundingRadio::buildHciTestEndCommand(
    BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpTestEnd;
  return true;
}

bool BleChannelSoundingRadio::parseHciTestEndCompleteEvent(
    const uint8_t* eventData,
    size_t eventLen,
    BleCsTestEndComplete* outEvent) {
  if (eventData == nullptr || outEvent == nullptr || eventLen != 1U) {
    return false;
  }
  outEvent->status = eventData[0U];
  return true;
}

bool BleChannelSoundingRadio::parseHciTestCompleteEvent(
    const uint8_t* eventData, size_t eventLen, BleCsTestComplete* outEvent) {
  return parseHciTestEndCompleteEvent(eventData, eventLen, outEvent);
}

// ─── Zephyr Parity: Channel Classification ───────────────────────

static constexpr size_t kBleCsChannelClassificationLen = 10U;

bool BleChannelSoundingRadio::buildHciSetChannelClassificationCommand(
    const BleCsChannelClassification& classification,
    BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) return false;
  BleCsHciCommand cmd = {};
  cmd.opcode = kBleCsHciOpSetChannelClassification;
  memcpy(cmd.payload, classification.channelMap, kBleCsChannelMapBytes);
  cmd.payloadLen = kBleCsChannelMapBytes;
  *outCommand = cmd;
  return true;
}

// ─── Zephyr Parity: FAE Table ───────────────────────────────────

bool BleChannelSoundingRadio::buildHciReadRemoteFaeTableCommand(
    uint16_t connHandle, BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpReadRemoteFaeTable;
  writeLe16(outCommand->payload, connHandle);
  outCommand->payloadLen = 2U;
  return true;
}

bool BleChannelSoundingRadio::parseHciReadRemoteFaeTableCompleteEvent(
    const uint8_t* eventData, size_t eventLen, BleCsFaeTable* outTable) {
  static constexpr size_t kFaeCompleteEventLen =
      3U + kBleCsFaeTableValueCount;
  if (eventData == nullptr || outTable == nullptr ||
      eventLen != kFaeCompleteEventLen) {
    return false;
  }
  BleCsFaeTable table{};
  table.status = eventData[0U];
  table.connHandle = readLe16(eventData + 1U);
  memcpy(table.values, eventData + 3U, sizeof(table.values));
  table.valid = (table.status == 0U);
  *outTable = table;
  return true;
}

bool BleChannelSoundingRadio::buildHciWriteCachedRemoteFaeTableCommand(
    uint16_t connHandle,
    const int8_t faeTable[kBleCsFaeTableValueCount],
    BleCsHciCommand* outCommand) {
  if (faeTable == nullptr || outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpWriteCachedRemoteFaeTable;
  writeLe16(outCommand->payload, connHandle);
  memcpy(outCommand->payload + 2U, faeTable, kBleCsFaeTableValueCount);
  outCommand->payloadLen =
      static_cast<uint8_t>(2U + kBleCsFaeTableValueCount);
  return true;
}

// ─── Zephyr Parity: Cached Capabilities ──────────────────────────

bool BleChannelSoundingRadio::buildHciWriteCachedRemoteSupportedCapabilitiesCommand(
    uint16_t connHandle,
    const BleCsControllerCapabilities& capabilities,
    BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpWriteCachedRemoteSupportedCapabilities;
  uint8_t* payload = outCommand->payload;
  writeLe16(payload + 0U, connHandle);
  payload[2U] = capabilities.numConfigSupported;
  writeLe16(payload + 3U, capabilities.maxConsecutiveProceduresSupported);
  payload[5U] = capabilities.numAntennasSupported;
  payload[6U] = capabilities.maxAntennaPathsSupported;
  payload[7U] = static_cast<uint8_t>(
      (capabilities.initiatorSupported ? 0x01U : 0U) |
      (capabilities.reflectorSupported ? 0x02U : 0U));
  payload[8U] = capabilities.mode3Supported ? 0x01U : 0U;
  payload[9U] = capabilities.rttCapability;
  payload[10U] = capabilities.rttAaOnlyN;
  payload[11U] = capabilities.rttSoundingN;
  payload[12U] = capabilities.rttRandomPayloadN;
  writeLe16(payload + 13U, capabilities.nadmSoundingCapability);
  writeLe16(payload + 15U, capabilities.nadmRandomCapability);
  payload[17U] = static_cast<uint8_t>(
      (capabilities.csSync2mPhySupported ? 0x02U : 0U) |
      (capabilities.csSync2m2btPhySupported ? 0x04U : 0U));
  const uint16_t subfeatures = static_cast<uint16_t>(
      (capabilities.csWithoutFaeSupported ? 0x0002U : 0U) |
      (capabilities.chselAlg3cSupported ? 0x0004U : 0U) |
      (capabilities.pbrFromRttSoundingSeqSupported ? 0x0008U : 0U));
  writeLe16(payload + 18U, subfeatures);
  writeLe16(payload + 20U, capabilities.tIp1TimesSupported);
  writeLe16(payload + 22U, capabilities.tIp2TimesSupported);
  writeLe16(payload + 24U, capabilities.tFcsTimesSupported);
  writeLe16(payload + 26U, capabilities.tPmTimesSupported);
  payload[28U] = capabilities.tSwTimeSupported;
  payload[29U] = capabilities.txSnrCapability;
  outCommand->payloadLen = 30U;
  return true;
}

bool BleChannelSoundingRadio::buildHciWriteCachedRemoteSupportedCapabilitiesV2Command(
    uint16_t connHandle,
    const BleCsControllerCapabilities& capabilities,
    BleCsHciCommand* outCommand) {
  if (!buildHciWriteCachedRemoteSupportedCapabilitiesCommand(
          connHandle, capabilities, outCommand)) {
    return false;
  }
  outCommand->opcode = kBleCsHciOpWriteCachedRemoteSupportedCapabilitiesV2;
  uint16_t subfeatures = readLe16(outCommand->payload + 18U);
  if (capabilities.csIptReflectorSupported) {
    subfeatures |= 0x0010U;
  }
  writeLe16(outCommand->payload + 18U, subfeatures);
  writeLe16(outCommand->payload + 30U,
            capabilities.tIp2IptTimesSupported);
  outCommand->payload[32U] = capabilities.tSwIptTimeSupported;
  outCommand->payloadLen = 33U;
  return true;
}

// ─── End Zephyr Parity additions ────────────────────────────────

BleCsSubeventResultReassembler::BleCsSubeventResultReassembler()
    : header_{}, stepData_{0}, stepDataLen_(0U), active_(false) {}

void BleCsSubeventResultReassembler::reset() {
  header_ = BleCsSubeventResultHeader{};
  memset(stepData_, 0, sizeof(stepData_));
  stepDataLen_ = 0U;
  active_ = false;
}

bool BleCsSubeventResultReassembler::active() const { return active_; }

bool BleCsSubeventResultReassembler::appendStepData(const uint8_t* data, size_t len) {
  if (len == 0U) {
    return true;
  }
  if (data == nullptr || (static_cast<size_t>(stepDataLen_) + len) > sizeof(stepData_)) {
    return false;
  }
  memcpy(stepData_ + stepDataLen_, data, len);
  stepDataLen_ = static_cast<uint16_t>(stepDataLen_ + len);
  return true;
}

void BleCsSubeventResultReassembler::fillOutput(bool complete,
                                                bool continuation,
                                                BleCsSubeventResult* outResult) const {
  if (outResult == nullptr) {
    return;
  }
  outResult->header = header_;
  outResult->stepData = (stepDataLen_ > 0U) ? stepData_ : nullptr;
  outResult->stepDataLen = stepDataLen_;
  outResult->isPartial = !complete;
  outResult->isComplete = complete;
  outResult->isContinuation = continuation;
}

bool BleCsSubeventResultReassembler::consumeInitialEvent(const uint8_t* eventData,
                                                         size_t eventLen,
                                                         BleCsSubeventResult* outResult) {
  BleCsSubeventResult parsed{};
  if (!BleChannelSoundingRadio::parseHciSubeventResultEvent(eventData, eventLen, &parsed)) {
    return false;
  }

  if (!parsed.isPartial) {
    reset();
    header_ = parsed.header;
    if (!appendStepData(parsed.stepData, parsed.stepDataLen)) {
      reset();
      return false;
    }
    active_ = false;
    fillOutput(true, false, outResult);
    return true;
  }

  if (parsed.header.procedureDoneStatus != kBleCsProcedureDonePartial ||
      parsed.header.numStepsReported == 0U || parsed.stepData == nullptr ||
      parsed.stepDataLen == 0U) {
    reset();
    return false;
  }

  reset();
  header_ = parsed.header;
  if (!appendStepData(parsed.stepData, parsed.stepDataLen)) {
    reset();
    return false;
  }
  active_ = true;
  fillOutput(false, false, outResult);
  return true;
}

bool BleCsSubeventResultReassembler::consumeContinuationEvent(const uint8_t* eventData,
                                                              size_t eventLen,
                                                              BleCsSubeventResult* outResult) {
  BleCsSubeventResult parsed{};
  if (!BleChannelSoundingRadio::parseHciSubeventResultContinueEvent(eventData, eventLen,
                                                                    &parsed)) {
    return false;
  }
  if (!active_ || parsed.header.connHandle != header_.connHandle ||
      parsed.header.configId != header_.configId) {
    return false;
  }
  if (parsed.header.numAntennaPaths != header_.numAntennaPaths) {
    return false;
  }
  if (!appendStepData(parsed.stepData, parsed.stepDataLen)) {
    reset();
    return false;
  }

  header_.procedureDoneStatus = parsed.header.procedureDoneStatus;
  header_.subeventDoneStatus = parsed.header.subeventDoneStatus;
  header_.procedureAbortReason = parsed.header.procedureAbortReason;
  header_.subeventAbortReason = parsed.header.subeventAbortReason;
  header_.numStepsReported = static_cast<uint16_t>(
      header_.numStepsReported + parsed.header.numStepsReported);

  const bool complete = !parsed.isPartial;
  fillOutput(complete, true, outResult);
  if (complete) {
    active_ = false;
  }
  return true;
}

BleCsControllerWorkflow::BleCsControllerWorkflow() : config_{}, state_{} {}

void BleCsControllerWorkflow::reset() {
  config_ = BleCsControllerWorkflowConfig{};
  state_ = BleCsControllerWorkflowState{};
}

bool BleCsControllerWorkflow::begin(uint16_t connHandle,
                                    const BleCsControllerWorkflowConfig& config) {
  reset();
  if (connHandle == 0U) {
    return false;
  }
  config_ = config;
  state_.connHandle = connHandle;
  state_.phase = BleCsControllerWorkflowPhase::kNeedReadRemoteCapabilities;
  return true;
}

bool BleCsControllerWorkflow::active() const {
  return state_.phase != BleCsControllerWorkflowPhase::kIdle &&
         state_.phase != BleCsControllerWorkflowPhase::kReady &&
         state_.phase != BleCsControllerWorkflowPhase::kFailed;
}

bool BleCsControllerWorkflow::ready() const {
  return state_.phase == BleCsControllerWorkflowPhase::kReady;
}

bool BleCsControllerWorkflow::failed() const {
  return state_.phase == BleCsControllerWorkflowPhase::kFailed;
}

BleCsControllerWorkflowPhase BleCsControllerWorkflow::phase() const {
  return state_.phase;
}

const BleCsControllerWorkflowState& BleCsControllerWorkflow::state() const {
  return state_;
}

const BleCsControllerWorkflowConfig& BleCsControllerWorkflow::config() const {
  return config_;
}

bool BleCsControllerWorkflow::buildNextCommand(BleCsHciCommand* outCommand) {
  if (outCommand == nullptr || failed() || ready() || state_.connHandle == 0U) {
    return false;
  }

  switch (state_.phase) {
    case BleCsControllerWorkflowPhase::kNeedReadRemoteCapabilities:
      if (!BleChannelSoundingRadio::buildHciReadRemoteSupportedCapabilitiesCommand(
              state_.connHandle, outCommand)) {
        return false;
      }
      state_.lastOpcode = outCommand->opcode;
      state_.phase = BleCsControllerWorkflowPhase::kWaitingRemoteCapabilities;
      return true;

    case BleCsControllerWorkflowPhase::kNeedSetDefaultSettings:
      if (!BleChannelSoundingRadio::buildHciSetDefaultSettingsCommand(
              state_.connHandle, config_.defaultSettings, outCommand)) {
        return false;
      }
      state_.lastOpcode = outCommand->opcode;
      state_.phase = BleCsControllerWorkflowPhase::kWaitingSetDefaultSettings;
      return true;

    case BleCsControllerWorkflowPhase::kNeedCreateConfig:
      if (!BleChannelSoundingRadio::buildHciCreateConfigCommand(
              state_.connHandle, config_.createConfig, outCommand)) {
        return false;
      }
      state_.lastOpcode = outCommand->opcode;
      state_.phase = BleCsControllerWorkflowPhase::kWaitingConfigComplete;
      return true;

    case BleCsControllerWorkflowPhase::kNeedSecurityEnable:
      if (!BleChannelSoundingRadio::buildHciSecurityEnableCommand(
              state_.connHandle, outCommand)) {
        return false;
      }
      state_.lastOpcode = outCommand->opcode;
      state_.phase = BleCsControllerWorkflowPhase::kWaitingSecurityEnableComplete;
      return true;

    case BleCsControllerWorkflowPhase::kNeedSetProcedureParameters:
      if (!BleChannelSoundingRadio::buildHciSetProcedureParametersCommand(
              state_.connHandle, config_.procedureParameters, outCommand)) {
        return false;
      }
      state_.lastOpcode = outCommand->opcode;
      state_.phase = BleCsControllerWorkflowPhase::kWaitingSetProcedureParameters;
      return true;

    case BleCsControllerWorkflowPhase::kNeedProcedureEnable:
      if (!BleChannelSoundingRadio::buildHciProcedureEnableCommand(
              state_.connHandle, config_.procedureEnable, outCommand)) {
        return false;
      }
      state_.lastOpcode = outCommand->opcode;
      state_.phase = BleCsControllerWorkflowPhase::kWaitingProcedureEnableComplete;
      return true;

    default:
      return false;
  }
}

bool BleCsControllerWorkflow::acknowledgeCommandStatus(uint16_t opcode, uint8_t status) {
  if (failed() || ready() || opcode != state_.lastOpcode) {
    return false;
  }

  state_.lastStatus = status;
  if (status != 0U) {
    fail(status);
    return true;
  }

  switch (state_.phase) {
    case BleCsControllerWorkflowPhase::kWaitingSetDefaultSettings:
      state_.defaultSettingsApplied = true;
      state_.phase = BleCsControllerWorkflowPhase::kNeedCreateConfig;
      return true;

    case BleCsControllerWorkflowPhase::kWaitingSetProcedureParameters:
      state_.procedureParametersApplied = true;
      state_.phase = BleCsControllerWorkflowPhase::kNeedProcedureEnable;
      return true;

    case BleCsControllerWorkflowPhase::kWaitingRemoteCapabilities:
    case BleCsControllerWorkflowPhase::kWaitingConfigComplete:
    case BleCsControllerWorkflowPhase::kWaitingSecurityEnableComplete:
    case BleCsControllerWorkflowPhase::kWaitingProcedureEnableComplete:
      return true;

    default:
      return false;
  }
}

bool BleCsControllerWorkflow::acknowledgeReadyCommandStatus(uint16_t opcode, uint8_t status) {
  if (failed() || !ready()) {
    return false;
  }

  state_.lastOpcode = opcode;
  state_.lastStatus = status;

  switch (opcode) {
    case kBleCsHciOpReadRemoteSupportedCapabilities:
    case kBleCsHciOpCreateConfig:
    case kBleCsHciOpRemoveConfig:
    case kBleCsHciOpSecurityEnable:
    case kBleCsHciOpProcedureEnable:
      return true;

    case kBleCsHciOpSetDefaultSettings:
      if (status == 0U) {
        state_.defaultSettingsApplied = true;
      }
      return true;

    case kBleCsHciOpSetProcedureParameters:
      if (status == 0U) {
        state_.procedureParametersApplied = true;
      }
      return true;

    default:
      return false;
  }
}

bool BleCsControllerWorkflow::consumeEvent(uint8_t subeventCode,
                                           const uint8_t* eventData,
                                           size_t eventLen) {
  if (eventData == nullptr || failed() || ready()) {
    return false;
  }

  switch (subeventCode) {
    case kBleCsHciEvtReadRemoteSupportedCapabilitiesComplete: {
      BleCsControllerCapabilities capabilities{};
      if (state_.phase != BleCsControllerWorkflowPhase::kWaitingRemoteCapabilities ||
          !BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteEvent(
              eventData, eventLen, &capabilities) ||
          capabilities.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = capabilities.status;
      if (capabilities.status != 0U) {
        fail(capabilities.status);
        return true;
      }
      if (!validateConfigAgainstCapabilities(config_, capabilities)) {
        fail(0x12U);
        return true;
      }
      state_.remoteCapabilities = capabilities;
      state_.remoteCapabilitiesValid = capabilities.valid;
      state_.phase = config_.applyDefaultSettings
                         ? BleCsControllerWorkflowPhase::kNeedSetDefaultSettings
                         : BleCsControllerWorkflowPhase::kNeedCreateConfig;
      return true;
    }

    case kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2: {
      BleCsControllerCapabilities capabilities{};
      if (state_.phase != BleCsControllerWorkflowPhase::kWaitingRemoteCapabilities ||
          !BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteV2Event(
              eventData, eventLen, &capabilities) ||
          capabilities.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = capabilities.status;
      if (capabilities.status != 0U) {
        fail(capabilities.status);
        return true;
      }
      if (!validateConfigAgainstCapabilities(config_, capabilities)) {
        fail(0x12U);
        return true;
      }
      state_.remoteCapabilities = capabilities;
      state_.remoteCapabilitiesValid = capabilities.valid;
      state_.phase = config_.applyDefaultSettings
                         ? BleCsControllerWorkflowPhase::kNeedSetDefaultSettings
                         : BleCsControllerWorkflowPhase::kNeedCreateConfig;
      return true;
    }

    case kBleCsHciEvtConfigComplete: {
      BleCsConfigComplete complete{};
      if (state_.phase != BleCsControllerWorkflowPhase::kWaitingConfigComplete ||
          !BleChannelSoundingRadio::parseHciConfigCompleteEvent(eventData, eventLen,
                                                                &complete) ||
          complete.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = complete.status;
      state_.configComplete = complete;
      if (complete.status != 0U) {
        fail(complete.status);
        return true;
      }
      state_.configCreated = (complete.action != 0U);
      state_.phase = config_.requireSecurityEnable
                         ? BleCsControllerWorkflowPhase::kNeedSecurityEnable
                         : BleCsControllerWorkflowPhase::kNeedSetProcedureParameters;
      return true;
    }

    case kBleCsHciEvtSecurityEnableComplete: {
      BleCsSecurityEnableComplete complete{};
      if (state_.phase != BleCsControllerWorkflowPhase::kWaitingSecurityEnableComplete ||
          !BleChannelSoundingRadio::parseHciSecurityEnableCompleteEvent(eventData, eventLen,
                                                                        &complete) ||
          complete.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = complete.status;
      if (complete.status != 0U) {
        fail(complete.status);
        return true;
      }
      state_.securityEnabled = true;
      state_.phase = BleCsControllerWorkflowPhase::kNeedSetProcedureParameters;
      return true;
    }

    case kBleCsHciEvtProcedureEnableComplete: {
      BleCsProcedureEnableComplete complete{};
      if (state_.phase != BleCsControllerWorkflowPhase::kWaitingProcedureEnableComplete ||
          !BleChannelSoundingRadio::parseHciProcedureEnableCompleteEvent(eventData, eventLen,
                                                                         &complete) ||
          complete.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = complete.status;
      state_.procedureEnableComplete = complete;
      if (complete.status != 0U) {
        fail(complete.status);
        return true;
      }
      state_.procedureEnabled = (complete.state != 0U);
      state_.phase = BleCsControllerWorkflowPhase::kReady;
      return true;
    }

    default:
      return false;
  }
}

bool BleCsControllerWorkflow::consumeReadyEvent(uint8_t subeventCode,
                                                const uint8_t* eventData,
                                                size_t eventLen) {
  if (eventData == nullptr || failed() || !ready()) {
    return false;
  }

  switch (subeventCode) {
    case kBleCsHciEvtReadRemoteSupportedCapabilitiesComplete: {
      BleCsControllerCapabilities capabilities{};
      if (!BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteEvent(
              eventData, eventLen, &capabilities) ||
          capabilities.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = capabilities.status;
      if (capabilities.status == 0U) {
        state_.remoteCapabilities = capabilities;
        state_.remoteCapabilitiesValid = capabilities.valid;
      }
      return true;
    }

    case kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2: {
      BleCsControllerCapabilities capabilities{};
      if (!BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteV2Event(
              eventData, eventLen, &capabilities) ||
          capabilities.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = capabilities.status;
      if (capabilities.status == 0U) {
        state_.remoteCapabilities = capabilities;
        state_.remoteCapabilitiesValid = capabilities.valid;
      }
      return true;
    }

    case kBleCsHciEvtConfigComplete: {
      BleCsConfigComplete complete{};
      if (!BleChannelSoundingRadio::parseHciConfigCompleteEvent(eventData, eventLen,
                                                                &complete) ||
          complete.connHandle != state_.connHandle) {
        return false;
      }
      const BleCsConfigComplete previousConfigComplete = state_.configComplete;
      const BleCsProcedureEnableComplete previousProcedureEnable =
          state_.procedureEnableComplete;
      state_.lastStatus = complete.status;
      state_.configComplete = complete;
      if (complete.status != 0U) {
        return true;
      }
      const bool removedActiveConfig =
          complete.action == 0U &&
          ((previousProcedureEnable.configId != 0U &&
            previousProcedureEnable.configId == complete.configId) ||
           (previousProcedureEnable.configId == 0U &&
            previousConfigComplete.action != 0U &&
            previousConfigComplete.configId == complete.configId));
      if (complete.action != 0U) {
        state_.configCreated = true;
      } else if (removedActiveConfig) {
        state_.configCreated = false;
        state_.remoteCapabilities = BleCsControllerCapabilities{};
        state_.remoteCapabilitiesValid = false;
        state_.defaultSettingsApplied = false;
        state_.securityEnabled = false;
      }
      if (complete.action != 0U || removedActiveConfig) {
        state_.procedureParametersApplied = false;
        state_.procedureEnabled = false;
        state_.procedureEnableComplete = BleCsProcedureEnableComplete{};
        state_.procedureEnableComplete.connHandle = complete.connHandle;
        state_.procedureEnableComplete.configId = complete.configId;
      }
      return true;
    }

    case kBleCsHciEvtSecurityEnableComplete: {
      BleCsSecurityEnableComplete complete{};
      if (!BleChannelSoundingRadio::parseHciSecurityEnableCompleteEvent(eventData, eventLen,
                                                                        &complete) ||
          complete.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = complete.status;
      if (complete.status == 0U) {
        state_.securityEnabled = true;
      }
      return true;
    }

    case kBleCsHciEvtProcedureEnableComplete: {
      BleCsProcedureEnableComplete complete{};
      if (!BleChannelSoundingRadio::parseHciProcedureEnableCompleteEvent(eventData, eventLen,
                                                                         &complete) ||
          complete.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = complete.status;
      state_.procedureEnableComplete = complete;
      if (complete.status == 0U) {
        state_.procedureEnabled = (complete.state != 0U);
        if (complete.state != 0U) {
          state_.procedureParametersApplied = true;
        }
      }
      return true;
    }

    default:
      return false;
  }
}

bool BleCsControllerWorkflow::consumeHciEventPacket(const uint8_t* packet, size_t packetLen) {
  BleCsHciCommandStatusEvent statusEvent{};
  if (BleChannelSoundingRadio::parseHciCommandStatusEvent(packet, packetLen, &statusEvent)) {
    return ready() ? acknowledgeReadyCommandStatus(statusEvent.opcode, statusEvent.status)
                   : acknowledgeCommandStatus(statusEvent.opcode, statusEvent.status);
  }

  BleCsHciCommandCompleteEvent completeEvent{};
  if (BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen, &completeEvent)) {
    return ready() ? acknowledgeReadyCommandStatus(completeEvent.opcode, completeEvent.status)
                   : acknowledgeCommandStatus(completeEvent.opcode, completeEvent.status);
  }

  BleCsHciLeMetaEvent metaEvent{};
  if (BleChannelSoundingRadio::parseHciLeMetaEvent(packet, packetLen, &metaEvent)) {
    return ready() ? consumeReadyEvent(metaEvent.subeventCode, metaEvent.payload,
                                       metaEvent.payloadLen)
                   : consumeEvent(metaEvent.subeventCode, metaEvent.payload,
                                  metaEvent.payloadLen);
  }

  return false;
}

void BleCsControllerWorkflow::reconcileReadyShadowState(uint8_t selectedConfigId,
                                                        bool sessionOpen,
                                                        bool configCreated,
                                                        bool securityEnabled,
                                                        bool procedureParametersApplied,
                                                        bool procedureEnabled) {
  if (failed()) {
    return;
  }

  if (!sessionOpen) {
    const bool workflowHadControllerState =
        ready() ||
        state_.configCreated ||
        state_.securityEnabled ||
        state_.procedureParametersApplied ||
        state_.procedureEnabled ||
        state_.configComplete.configId != 0U ||
        state_.procedureEnableComplete.configId != 0U;
    if (!workflowHadControllerState) {
      return;
    }

    state_.remoteCapabilities = BleCsControllerCapabilities{};
    state_.remoteCapabilitiesValid = false;
    state_.defaultSettingsApplied = false;
    state_.configCreated = false;
    state_.securityEnabled = false;
    state_.procedureParametersApplied = false;
    state_.procedureEnabled = false;
    state_.procedureEnableComplete = BleCsProcedureEnableComplete{};
    state_.procedureEnableComplete.connHandle = state_.connHandle;
    /* Parity item #3: return workflow state machine to idle on disconnect. */
    state_.phase = BleCsControllerWorkflowPhase::kIdle;
    return;
  }

  if (!state_.configCreated && configCreated && selectedConfigId != 0U) {
    state_.remoteCapabilitiesValid = true;
    state_.defaultSettingsApplied = true;
    state_.configCreated = true;
    state_.securityEnabled = securityEnabled;
    state_.procedureParametersApplied = procedureParametersApplied;
    state_.configComplete.connHandle = state_.connHandle;
    state_.configComplete.status = 0U;
    state_.configComplete.action = 1U;
    state_.configComplete.configId = selectedConfigId;
  }

  if ((state_.phase == BleCsControllerWorkflowPhase::kNeedSecurityEnable ||
       state_.phase == BleCsControllerWorkflowPhase::kWaitingSecurityEnableComplete) &&
      securityEnabled) {
    state_.lastStatus = 0U;
    state_.securityEnabled = true;
    state_.phase = BleCsControllerWorkflowPhase::kNeedSetProcedureParameters;
  }

  if ((state_.phase == BleCsControllerWorkflowPhase::kNeedSetProcedureParameters ||
       state_.phase == BleCsControllerWorkflowPhase::kWaitingSetProcedureParameters) &&
      procedureParametersApplied) {
    state_.lastStatus = 0U;
    state_.procedureParametersApplied = true;
    state_.phase = BleCsControllerWorkflowPhase::kNeedProcedureEnable;
  }

  if ((state_.phase == BleCsControllerWorkflowPhase::kNeedProcedureEnable ||
       state_.phase == BleCsControllerWorkflowPhase::kWaitingProcedureEnableComplete) &&
      procedureEnabled) {
    state_.lastStatus = 0U;
    state_.procedureEnabled = true;
    if (selectedConfigId != 0U) {
      state_.procedureEnableComplete.connHandle = state_.connHandle;
      state_.procedureEnableComplete.status = 0U;
      state_.procedureEnableComplete.configId = selectedConfigId;
      state_.procedureEnableComplete.state = 1U;
    }
    state_.phase = BleCsControllerWorkflowPhase::kReady;
  }

  if (state_.configCreated && configCreated && selectedConfigId != 0U &&
      state_.configComplete.action != 0U) {
    state_.configComplete.connHandle = state_.connHandle;
    state_.configComplete.status = 0U;
    state_.configComplete.configId = selectedConfigId;
  }

  if (state_.procedureEnabled != procedureEnabled) {
    state_.procedureEnabled = procedureEnabled;
    if (selectedConfigId != 0U) {
      state_.procedureEnableComplete.connHandle = state_.connHandle;
      state_.procedureEnableComplete.configId = selectedConfigId;
      state_.procedureEnableComplete.state = procedureEnabled ? 1U : 0U;
    }
  }
}

const char* BleCsControllerWorkflow::phaseName(BleCsControllerWorkflowPhase phase) {
  switch (phase) {
    case BleCsControllerWorkflowPhase::kIdle:
      return "idle";
    case BleCsControllerWorkflowPhase::kNeedReadRemoteCapabilities:
      return "need_read_remote_caps";
    case BleCsControllerWorkflowPhase::kWaitingRemoteCapabilities:
      return "waiting_remote_caps";
    case BleCsControllerWorkflowPhase::kNeedSetDefaultSettings:
      return "need_set_defaults";
    case BleCsControllerWorkflowPhase::kWaitingSetDefaultSettings:
      return "waiting_set_defaults";
    case BleCsControllerWorkflowPhase::kNeedCreateConfig:
      return "need_create_config";
    case BleCsControllerWorkflowPhase::kWaitingConfigComplete:
      return "waiting_config_complete";
    case BleCsControllerWorkflowPhase::kNeedSecurityEnable:
      return "need_security_enable";
    case BleCsControllerWorkflowPhase::kWaitingSecurityEnableComplete:
      return "waiting_security_enable";
    case BleCsControllerWorkflowPhase::kNeedSetProcedureParameters:
      return "need_set_procedure_params";
    case BleCsControllerWorkflowPhase::kWaitingSetProcedureParameters:
      return "waiting_set_procedure_params";
    case BleCsControllerWorkflowPhase::kNeedProcedureEnable:
      return "need_procedure_enable";
    case BleCsControllerWorkflowPhase::kWaitingProcedureEnableComplete:
      return "waiting_procedure_enable";
    case BleCsControllerWorkflowPhase::kReady:
      return "ready";
    case BleCsControllerWorkflowPhase::kFailed:
      return "failed";
    default:
      return "unknown";
  }
}

bool BleCsControllerWorkflow::validateConfigAgainstCapabilities(
    const BleCsControllerWorkflowConfig& config,
    const BleCsControllerCapabilities& capabilities) {
  if (!capabilities.valid) {
    return false;
  }

  if ((config.createConfig.mainModeType == kBleCsMainMode3 ||
       config.createConfig.subModeType == kBleCsMainMode3) &&
      !capabilities.mode3Supported) {
    return false;
  }

  if (config.createConfig.role == 0U && !capabilities.initiatorSupported) {
    return false;
  }
  if (config.createConfig.role == 1U && !capabilities.reflectorSupported) {
    return false;
  }

  if (config.createConfig.csSyncPhy == 2U && !capabilities.csSync2mPhySupported) {
    return false;
  }
  if (config.createConfig.csSyncPhy == 3U && !capabilities.csSync2m2btPhySupported) {
    return false;
  }

  if (config.createConfig.rttType == 1U || config.createConfig.rttType == 2U) {
    if (capabilities.rttSoundingN == 0U) {
      return false;
    }
  } else if (config.createConfig.rttType >= 3U) {
    if (capabilities.rttRandomPayloadN == 0U) {
      return false;
    }
  }

  if (config.createConfig.channelSelectionType == 1U &&
      !capabilities.chselAlg3cSupported) {
    return false;
  }

  return true;
}

void BleCsControllerWorkflow::fail(uint8_t status) {
  state_.lastStatus = status;
  state_.phase = BleCsControllerWorkflowPhase::kFailed;
}

BleHciPacketStreamDecoder::BleHciPacketStreamDecoder()
    : buffer_{0},
      used_(0U),
      expected_(0U),
      acceptedTypes_(packetTypeMask(kBleHciPacketTypeCommand) |
                     packetTypeMask(kBleHciPacketTypeEvent)),
      deliveredPackets_(0U),
      ignoredPackets_(0U),
      ignoredBytes_(0U) {}

void BleHciPacketStreamDecoder::reset() {
  resetBuffer();
  deliveredPackets_ = 0U;
  ignoredPackets_ = 0U;
  ignoredBytes_ = 0U;
}

void BleHciPacketStreamDecoder::setAcceptedPacketTypes(uint32_t acceptedTypes) {
  acceptedTypes_ = acceptedTypes;
}

uint32_t BleHciPacketStreamDecoder::acceptedPacketTypes() const { return acceptedTypes_; }

uint32_t BleHciPacketStreamDecoder::deliveredPacketCount() const { return deliveredPackets_; }

uint32_t BleHciPacketStreamDecoder::ignoredPacketCount() const { return ignoredPackets_; }

uint32_t BleHciPacketStreamDecoder::ignoredByteCount() const { return ignoredBytes_; }

uint32_t BleHciPacketStreamDecoder::packetTypeMask(uint8_t packetType) {
  return (packetType < 32U) ? (1UL << packetType) : 0UL;
}

bool BleHciPacketStreamDecoder::pushBytes(
    const uint8_t* data, size_t len,
    bool (*onPacket)(const uint8_t* packet, size_t packetLen, void* userData),
    void* userData) {
  if ((len > 0U && data == nullptr) || onPacket == nullptr) {
    return false;
  }

  for (size_t i = 0U; i < len; ++i) {
    if (used_ >= sizeof(buffer_)) {
      resetBuffer();
      return false;
    }
    buffer_[used_++] = data[i];
    if (!determineExpectedLength()) {
      resetBuffer();
      return false;
    }
    if (expectedLengthKnown() && used_ == expected_) {
      if (!packetTypeAccepted()) {
        ++ignoredPackets_;
        ignoredBytes_ = static_cast<uint32_t>(ignoredBytes_ + used_);
        resetBuffer();
        continue;
      }
      const bool ok = onPacket(buffer_, used_, userData);
      resetBuffer();
      if (!ok) {
        return false;
      }
      ++deliveredPackets_;
    } else if (expectedLengthKnown() && used_ > expected_) {
      resetBuffer();
      return false;
    }
  }
  return true;
}

bool BleHciPacketStreamDecoder::expectedLengthKnown() const { return expected_ > 0U; }

bool BleHciPacketStreamDecoder::determineExpectedLength() {
  if (expected_ > 0U || used_ == 0U) {
    return true;
  }

  switch (buffer_[0U]) {
    case kBleHciPacketTypeEvent:
      if (used_ >= 3U) {
        expected_ = static_cast<size_t>(3U + buffer_[2U]);
      }
      return true;

    case kBleHciPacketTypeCommand:
      if (used_ >= 4U) {
        expected_ = static_cast<size_t>(4U + buffer_[3U]);
      }
      return true;

    case kBleHciPacketTypeAcl:
      if (used_ >= 5U) {
        expected_ = static_cast<size_t>(5U + readLe16(buffer_ + 3U));
      }
      return true;

    case kBleHciPacketTypeSco:
      if (used_ >= 4U) {
        expected_ = static_cast<size_t>(4U + buffer_[3U]);
      }
      return true;

    case kBleHciPacketTypeIso:
      if (used_ >= 5U) {
        expected_ = static_cast<size_t>(5U + (readLe16(buffer_ + 3U) & 0x3FFFU));
      }
      return true;

    default:
      return false;
  }
}

bool BleHciPacketStreamDecoder::packetTypeAccepted() const {
  if (used_ == 0U) {
    return false;
  }
  return (acceptedTypes_ & packetTypeMask(buffer_[0U])) != 0U;
}

void BleHciPacketStreamDecoder::resetBuffer() {
  used_ = 0U;
  expected_ = 0U;
}

BleCsControllerSession::BleCsControllerSession()
    : config_{},
      state_{},
      workflow_{},
      workflowDecoder_{},
      localDecoder_{},
      peerDecoder_{},
      localReassembler_{},
      peerReassembler_{},
      localResult_{},
      peerResult_{},
      accumulatedLocalResult_{},
      accumulatedPeerResult_{},
      completedLocalResult_{},
      completedPeerResult_{},
      accumulatedLocalStepData_{0},
      accumulatedPeerStepData_{0},
      completedLocalStepData_{0},
      completedPeerStepData_{0} {}

void BleCsControllerSession::reset() {
  config_ = BleCsControllerSessionConfig{};
  state_ = BleCsControllerSessionState{};
  workflow_.reset();
  workflowDecoder_.reset();
  localDecoder_.reset();
  peerDecoder_.reset();
  workflowDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  localDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  peerDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  localReassembler_.reset();
  peerReassembler_.reset();
  localResult_ = BleCsSubeventResult{};
  peerResult_ = BleCsSubeventResult{};
  accumulatedLocalResult_ = BleCsSubeventResult{};
  accumulatedPeerResult_ = BleCsSubeventResult{};
  completedLocalResult_ = BleCsSubeventResult{};
  completedPeerResult_ = BleCsSubeventResult{};
  lastProcedureAbortReason_ = 0U;
  lastSubeventAbortReason_ = 0U;
}

bool parseDirectStatusResponse(const uint8_t* packet,
                               size_t packetLen,
                               uint16_t expectedOpcode,
                               uint8_t* outStatus) {
  if (outStatus == nullptr) {
    return false;
  }
  BleCsHciCommandStatusEvent statusEvent{};
  if (BleChannelSoundingRadio::parseHciCommandStatusEvent(packet, packetLen, &statusEvent) &&
      statusEvent.opcode == expectedOpcode) {
    *outStatus = statusEvent.status;
    return true;
  }

  BleCsHciCommandCompleteEvent completeEvent{};
  if (BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen,
                                                            &completeEvent) &&
      completeEvent.opcode == expectedOpcode) {
    *outStatus = completeEvent.status;
    return true;
  }

  return false;
}

bool parseVprPeerExchangeResponse(const uint8_t* packet,
                                  size_t packetLen,
                                  uint16_t expectedOpcode,
                                  BleCsVprPeerExchangeState* outState) {
  if (outState == nullptr) {
    return false;
  }
  *outState = BleCsVprPeerExchangeState{};

  BleCsHciCommandCompleteEvent completeEvent{};
  if (!BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen,
                                                             &completeEvent) ||
      completeEvent.opcode != expectedOpcode ||
      completeEvent.returnParams == nullptr ||
      completeEvent.returnParamsLen < 9U) {
    return false;
  }

  const uint8_t* params = completeEvent.returnParams;
  outState->valid = true;
  outState->status = params[0];
  outState->previousStage = params[1];
  outState->currentStage = params[2];
  outState->deadlineHeartbeat = readLe32(params + 3U);
  outState->procedureAbortReason = params[7];
  outState->subeventAbortReason = params[8];
  return true;
}

bool parseVprPendingLocalLlControlPduResponse(
    const uint8_t* packet,
    size_t packetLen,
    uint16_t expectedOpcode,
    BleCsLlControlPdu* outPdu,
    BleCsVprPeerExchangeState* outState) {
  if (outPdu == nullptr || outState == nullptr) {
    return false;
  }
  *outPdu = BleCsLlControlPdu{};
  *outState = BleCsVprPeerExchangeState{};

  BleCsHciCommandCompleteEvent completeEvent{};
  if (!BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen,
                                                             &completeEvent) ||
      completeEvent.opcode != expectedOpcode ||
      completeEvent.returnParams == nullptr ||
      completeEvent.returnParamsLen < 10U) {
    return false;
  }

  const uint8_t* params = completeEvent.returnParams;
  outState->valid = true;
  outState->status = params[0];
  outState->previousStage = params[1];
  outState->currentStage = params[2];
  outState->deadlineHeartbeat = readLe32(params + 3U);
  outState->procedureAbortReason = params[7];
  outState->subeventAbortReason = params[8];

  const uint8_t pduLen = params[9];
  if (outState->status != 0U || pduLen == 0U ||
      static_cast<size_t>(10U + pduLen) > completeEvent.returnParamsLen ||
      !bleCsLlControlPduIsValid(params + 10U, pduLen)) {
    return false;
  }

  memcpy(outPdu->bytes, params + 10U, pduLen);
  outPdu->length = pduLen;
  return true;
}

bool parseVprSchedulerStateResponse(const uint8_t* packet,
                                    size_t packetLen,
                                    uint16_t expectedOpcode,
                                    BleCsVprSchedulerState* outState) {
  if (outState == nullptr) {
    return false;
  }
  *outState = BleCsVprSchedulerState{};

  BleCsHciCommandCompleteEvent completeEvent{};
  if (!BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen,
                                                             &completeEvent) ||
      completeEvent.opcode != expectedOpcode ||
      completeEvent.returnParams == nullptr ||
      completeEvent.returnParamsLen < 55U) {
    return false;
  }

  const uint8_t* params = completeEvent.returnParams;
  outState->valid = true;
  outState->status = params[0];
  outState->flags = params[1];
  outState->sessionOpen = (params[1] & 0x01U) != 0U;
  outState->procedureEnabled = (params[1] & 0x02U) != 0U;
  outState->resultPending = (params[1] & 0x04U) != 0U;
  outState->peerProcedureActive = (params[1] & 0x08U) != 0U;
  outState->builtInPeerDemoEnabled = (params[1] & 0x10U) != 0U;
  outState->testActive = (params[1] & 0x20U) != 0U;
  outState->pendingResultStage = params[2];
  outState->activeSubeventIndex = params[3];
  outState->totalSubevents = params[4];
  outState->totalSteps = params[5];
  outState->subeventStartStep = params[6];
  outState->subeventStepCount = params[7];
  outState->localChunkStartStep = params[8];
  outState->peerChunkStartStep = params[9];
  outState->procedureCounter = readLe16(params + 10U);
  outState->connHandle = readLe16(params + 12U);
  outState->heartbeat = readLe32(params + 14U);
  outState->nextProcedureHeartbeat = readLe32(params + 18U);
  outState->nextSubeventHeartbeat = readLe32(params + 22U);
  outState->nextPeerStageHeartbeat = readLe32(params + 26U);
  outState->nextChunkStageHeartbeat = readLe32(params + 30U);
  outState->procedureIntervalTicks = readLe32(params + 34U);
  outState->subeventDelayTicks = readLe32(params + 38U);
  outState->peerDelayTicks = readLe32(params + 42U);
  outState->chunkDelayTicks = readLe32(params + 46U);
  outState->subeventEncodedStepBytes = readLe16(params + 50U);
  outState->configId = params[52];
  outState->intervalSelector = params[53];
  outState->peerGapTicks = params[54];
  return true;
}

bool parseVprSecurityMaterialResponse(const uint8_t* packet,
                                      size_t packetLen,
                                      uint16_t expectedOpcode,
                                      BleCsVprSecurityMaterialState* outState) {
  if (outState == nullptr) {
    return false;
  }
  *outState = BleCsVprSecurityMaterialState{};

  BleCsHciCommandCompleteEvent completeEvent{};
  if (!BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen,
                                                             &completeEvent) ||
      completeEvent.opcode != expectedOpcode ||
      completeEvent.returnParams == nullptr ||
      completeEvent.returnParamsLen < 22U) {
    return false;
  }

  const uint8_t* params = completeEvent.returnParams;
  outState->valid = true;
  outState->status = params[0];
  outState->flags = params[1];
  outState->materialValid = (params[1] & kBleCsSecurityMaterialFlagValid) != 0U;
  outState->controllerOwned =
      (params[1] & kBleCsSecurityMaterialFlagControllerOwned) != 0U;
  outState->boundToConfig =
      (params[1] & kBleCsSecurityMaterialFlagBoundToConfig) != 0U;
  outState->connHandle = readLe16(params + 2U);
  outState->configId = params[4];
  outState->materialValidRaw = params[5];
  outState->drbgNonce = readLe16(params + 6U);
  outState->procedureCounter = readLe16(params + 8U);
  outState->sessionCounter = readLe32(params + 10U);
  outState->materialToken = readLe32(params + 14U);
  outState->generationHeartbeat = readLe32(params + 18U);
  return true;
}

bool parseVprMeasurementWorkItemResponse(const uint8_t* packet,
                                         size_t packetLen,
                                         uint16_t expectedOpcode,
                                         BleCsVprMeasurementWorkItem* outWork) {
  if (outWork == nullptr) {
    return false;
  }
  *outWork = BleCsVprMeasurementWorkItem{};

  BleCsHciCommandCompleteEvent completeEvent{};
  if (!BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen,
                                                             &completeEvent) ||
      completeEvent.opcode != expectedOpcode ||
      completeEvent.returnParams == nullptr ||
      completeEvent.returnParamsLen < 55U) {
    return false;
  }

  const uint8_t* params = completeEvent.returnParams;
  outWork->valid = true;
  outWork->status = params[0];
  outWork->flags = params[1];
  outWork->sessionOpen = (params[1] & 0x01U) != 0U;
  outWork->procedureEnabled = (params[1] & 0x02U) != 0U;
  outWork->resultPending = (params[1] & 0x04U) != 0U;
  outWork->peerProcedureActive = (params[1] & 0x08U) != 0U;
  outWork->builtInPeerDemoEnabled = (params[1] & 0x10U) != 0U;
  outWork->testActive = (params[1] & 0x20U) != 0U;
  outWork->ready = (params[1] & 0x40U) != 0U || params[54] != 0U;
  outWork->controllerAutoExecuted = (params[1] & 0x80U) != 0U;
  outWork->activeSubeventIndex = params[2];
  outWork->totalSubevents = params[3];
  outWork->totalSteps = params[4];
  outWork->subeventStartStep = params[5];
  outWork->subeventStepCount = params[6];
  outWork->localChunkStartStep = params[7];
  outWork->peerChunkStartStep = params[8];
  outWork->intervalSelector = params[9];
  outWork->procedureCounter = readLe16(params + 10U);
  outWork->connHandle = readLe16(params + 12U);
  outWork->configId = params[14];
  outWork->peerGapTicks = params[15];
  outWork->subeventEncodedStepBytes = readLe16(params + 16U);
  outWork->heartbeat = readLe32(params + 18U);
  outWork->procedureIntervalTicks = readLe32(params + 22U);
  outWork->subeventDelayTicks = readLe32(params + 26U);
  outWork->peerDelayTicks = readLe32(params + 30U);
  outWork->chunkDelayTicks = readLe32(params + 34U);
  outWork->nextProcedureHeartbeat = readLe32(params + 38U);
  outWork->nextSubeventHeartbeat = readLe32(params + 42U);
  outWork->nextPeerStageHeartbeat = readLe32(params + 46U);
  outWork->nextChunkStageHeartbeat = readLe32(params + 50U);
  if (completeEvent.returnParamsLen >= 62U) {
    outWork->stepChannelCount = (params[55] <= sizeof(outWork->stepChannels))
                                    ? params[55]
                                    : sizeof(outWork->stepChannels);
    memcpy(outWork->stepChannels, params + 56U, sizeof(outWork->stepChannels));
  }
  if (completeEvent.returnParamsLen >= 64U) {
    outWork->controllerAutoBlockMask = readLe16(params + 62U);
  }
  if (completeEvent.returnParamsLen >= 80U) {
    outWork->controllerAutoCount = readLe32(params + 64U);
    outWork->controllerAutoServiceCalls = readLe32(params + 68U);
    outWork->controllerAutoDuePasses = readLe32(params + 72U);
    outWork->controllerAutoProcedureCounter = readLe16(params + 76U);
    outWork->controllerAutoSubevent = params[78];
    outWork->controllerAutoStatus = params[79];
  }
  return true;
}

bool parseVprMeasurementExecutionResponse(
    const uint8_t* packet,
    size_t packetLen,
    uint16_t expectedOpcode,
    BleCsVprMeasurementExecutionResult* outResult) {
  if (outResult == nullptr) {
    return false;
  }
  *outResult = BleCsVprMeasurementExecutionResult{};

  BleCsHciCommandCompleteEvent completeEvent{};
  if (!BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen,
                                                             &completeEvent) ||
      completeEvent.opcode != expectedOpcode ||
      completeEvent.returnParams == nullptr ||
      completeEvent.returnParamsLen < 32U) {
    return false;
  }

  const uint8_t* params = completeEvent.returnParams;
  outResult->valid = true;
  outResult->status = params[0];
  outResult->flags = params[1];
  outResult->ready = (params[1] & 0x01U) != 0U;
  outResult->accepted = (params[1] & 0x02U) != 0U;
  outResult->resultPending = (params[1] & 0x04U) != 0U;
  outResult->peerProcedureActive = (params[1] & 0x08U) != 0U;
  outResult->controllerOwnedSnapshot = (params[1] & 0x10U) != 0U;
  outResult->activeSubeventIndex = params[2];
  outResult->totalSubevents = params[3];
  outResult->totalSteps = params[4];
  outResult->subeventStartStep = params[5];
  outResult->subeventStepCount = params[6];
  outResult->configId = params[7];
  outResult->stepChannelCount = params[8];
  outResult->executedChannelCount = params[9];
  outResult->procedureCounter = readLe16(params + 10U);
  outResult->connHandle = readLe16(params + 12U);
  outResult->heartbeat = readLe32(params + 14U);
  outResult->executeCount = readLe32(params + 18U);
  memcpy(outResult->stepChannels, params + 22U, sizeof(outResult->stepChannels));
  if (completeEvent.returnParamsLen >= 36U) {
    outResult->executionToken = readLe32(params + 32U);
    outResult->executionTokenValid = outResult->executionToken != 0U;
  }
  if (completeEvent.returnParamsLen >= 60U) {
    outResult->rfDescriptorVersion = params[36];
    outResult->rfDescriptorFlags = params[37];
    outResult->rfDescriptorValid =
        outResult->rfDescriptorVersion == 1U &&
        ((outResult->rfDescriptorFlags & 0x01U) != 0U);
    outResult->rfRole = params[38];
    outResult->rfPhy = params[39];
    outResult->rfTxPowerDelta = static_cast<int8_t>(params[40]);
    outResult->rfRttType = params[41];
    outResult->rfStepChannelCount = params[42];
    outResult->rfMinSubeventLen = readLe32(params + 44U);
    outResult->rfMaxSubeventLen = readLe32(params + 48U);
    outResult->rfNextSubeventHeartbeat = readLe32(params + 52U);
    outResult->rfDescriptorToken = readLe32(params + 56U);
    outResult->rfDescriptorTokenValid = outResult->rfDescriptorToken != 0U;
  }
  if (completeEvent.returnParamsLen >= 80U) {
    outResult->rfPacketConfigToken =
        static_cast<uint32_t>(params[30]) |
        (static_cast<uint32_t>(params[31]) << 8U) |
        (static_cast<uint32_t>(params[62]) << 16U) |
        (static_cast<uint32_t>(params[63]) << 24U);
    outResult->rfPacketConfigTokenValid =
        outResult->rfPacketConfigToken != 0U;
    outResult->rfHardwareVersion = params[60];
    outResult->rfHardwareFlags = params[61];
    outResult->rfHardwareValid =
        outResult->rfHardwareVersion == 1U &&
        ((outResult->rfHardwareFlags & 0x01U) != 0U);
    outResult->rfHardwareState = readLe32(params + 64U);
    outResult->rfHardwareMode = readLe32(params + 68U);
    outResult->rfHardwareFrequency = readLe32(params + 72U);
    outResult->rfHardwareToken = readLe32(params + 76U);
    outResult->rfHardwareTokenValid = outResult->rfHardwareToken != 0U;
  }
  if (completeEvent.returnParamsLen >= 104U) {
    outResult->rfPrimitiveVersion = params[80];
    outResult->rfPrimitiveFlags = params[81];
    outResult->rfPrimitiveStatus = params[82];
    outResult->rfPrimitiveValid =
        outResult->rfPrimitiveVersion == 1U &&
        ((outResult->rfPrimitiveFlags & 0x01U) != 0U);
    outResult->rfPrimitivePllReady =
        (outResult->rfPrimitiveFlags & 0x02U) != 0U;
    outResult->rfPrimitiveDisabled =
        (outResult->rfPrimitiveFlags & 0x04U) != 0U;
    outResult->rfPrimitiveStateBefore = readLe32(params + 84U);
    outResult->rfPrimitivePllWaitLoops = readLe32(params + 88U);
    outResult->rfPrimitiveDisableWaitLoops = readLe32(params + 92U);
    outResult->rfPrimitiveStateAfter = readLe32(params + 96U);
    outResult->rfPrimitiveToken = readLe32(params + 100U);
    outResult->rfPrimitiveTokenValid = outResult->rfPrimitiveToken != 0U;
  }
  if (completeEvent.returnParamsLen >= 128U) {
    outResult->rfRetuneVersion = params[104];
    outResult->rfRetuneFlags = params[105];
    outResult->rfRetuneStatus = params[106];
    outResult->rfRetuneChannel = params[107];
    outResult->rfRetuneValid =
        outResult->rfRetuneVersion == 1U &&
        ((outResult->rfRetuneFlags & 0x01U) != 0U);
    outResult->rfRetuneModeWritten =
        (outResult->rfRetuneFlags & 0x02U) != 0U;
    outResult->rfRetuneFrequencyWritten =
        (outResult->rfRetuneFlags & 0x04U) != 0U;
    outResult->rfRetuneDatawhiteWritten =
        (outResult->rfRetuneFlags & 0x08U) != 0U;
    outResult->rfRetuneTargetFrequency = readLe32(params + 108U);
    outResult->rfRetuneTargetDatawhite = readLe32(params + 112U);
    outResult->rfRetuneObservedFrequency = readLe32(params + 116U);
    outResult->rfRetuneObservedDatawhite = readLe32(params + 120U);
    outResult->rfRetuneToken = readLe32(params + 124U);
    outResult->rfRetuneTokenValid = outResult->rfRetuneToken != 0U;
  }
  if (completeEvent.returnParamsLen >= 152U) {
    outResult->rfTimedMode2Version = params[128];
    outResult->rfTimedMode2Flags = params[129];
    outResult->rfTimedMode2Status = params[130];
    outResult->rfTimedMode2Channel = params[131];
    outResult->rfTimedMode2Valid =
        outResult->rfTimedMode2Version == 1U &&
        ((outResult->rfTimedMode2Flags & 0x01U) != 0U);
    outResult->rfTimedMode2TxReady =
        (outResult->rfTimedMode2Flags & 0x02U) != 0U;
    outResult->rfTimedMode2TxEnd =
        (outResult->rfTimedMode2Flags & 0x04U) != 0U;
    outResult->rfTimedMode2RxReady =
        (outResult->rfTimedMode2Flags & 0x08U) != 0U;
    outResult->rfTimedMode2RxEnd =
        (outResult->rfTimedMode2Flags & 0x10U) != 0U;
    outResult->rfTimedMode2Disabled =
        (outResult->rfTimedMode2Flags & 0x20U) != 0U;
    outResult->rfTimedMode2TimingApplied =
        (outResult->rfTimedMode2Flags & 0x40U) != 0U;
    outResult->rfTimedMode2PacketPtrRestored =
        (outResult->rfTimedMode2Flags & 0x80U) != 0U;
    outResult->rfTimedMode2TxWaitLoops = readLe32(params + 132U);
    outResult->rfTimedMode2GapWaitLoops = readLe32(params + 136U);
    outResult->rfTimedMode2RxReadyWaitLoops = readLe32(params + 140U);
    outResult->rfTimedMode2ListenWaitLoops = readLe32(params + 144U);
    outResult->rfTimedMode2DisableWaitLoops = 0U;
    outResult->rfTimedMode2StateAfter =
        outResult->rfTimedMode2Disabled ? 0U : 0xFFFFFFFFUL;
    outResult->rfTimedMode2Token = readLe32(params + 148U);
    outResult->rfTimedMode2TokenValid =
        outResult->rfTimedMode2Token != 0U;

    outResult->rfRxPrimitiveVersion = outResult->rfTimedMode2Version;
    outResult->rfRxPrimitiveFlags =
        (outResult->rfTimedMode2Valid ? 0x01U : 0x00U) |
        (outResult->rfTimedMode2RxReady ? 0x02U : 0x00U) |
        (outResult->rfTimedMode2Disabled ? 0x04U : 0x00U);
    outResult->rfRxPrimitiveStatus = outResult->rfTimedMode2Status;
    outResult->rfRxPrimitiveValid =
        outResult->rfRxPrimitiveVersion == 1U &&
        ((outResult->rfRxPrimitiveFlags & 0x01U) != 0U);
    outResult->rfRxPrimitiveRxReady =
        (outResult->rfRxPrimitiveFlags & 0x02U) != 0U;
    outResult->rfRxPrimitiveDisabled =
        (outResult->rfRxPrimitiveFlags & 0x04U) != 0U;
    outResult->rfRxPrimitiveStateBefore = 0U;
    outResult->rfRxPrimitiveRxReadyWaitLoops =
        outResult->rfTimedMode2RxReadyWaitLoops;
    outResult->rfRxPrimitiveDisableWaitLoops =
        outResult->rfTimedMode2ListenWaitLoops;
    outResult->rfRxPrimitiveStateAfter =
        outResult->rfTimedMode2Disabled ? 0U : 0xFFFFFFFFUL;
    outResult->rfRxPrimitiveToken = buildMeasurementRfPrimitiveToken(
        outResult->rfRxPrimitiveVersion,
        outResult->rfRxPrimitiveFlags,
        outResult->rfRxPrimitiveStatus,
        outResult->rfRxPrimitiveStateBefore,
        outResult->rfRxPrimitiveRxReadyWaitLoops,
        outResult->rfRxPrimitiveDisableWaitLoops,
        outResult->rfRxPrimitiveStateAfter);
    outResult->rfRxPrimitiveTokenValid =
        outResult->rfRxPrimitiveToken != 0U;
  }
  if (completeEvent.returnParamsLen >= 201U) {
    outResult->rfTimedMode2ObservedCount =
        (params[152] <= sizeof(outResult->rfTimedMode2ObservedChannels))
            ? params[152]
            : sizeof(outResult->rfTimedMode2ObservedChannels);
    memcpy(outResult->rfTimedMode2ObservedChannels, params + 153U,
           sizeof(outResult->rfTimedMode2ObservedChannels));
    memcpy(outResult->rfTimedMode2ObservedStatus, params + 159U,
           sizeof(outResult->rfTimedMode2ObservedStatus));
    memcpy(outResult->rfTimedMode2ObservedFlags, params + 165U,
           sizeof(outResult->rfTimedMode2ObservedFlags));
    memcpy(outResult->rfTimedMode2ObservedEventMask, params + 171U,
           sizeof(outResult->rfTimedMode2ObservedEventMask));
    for (uint8_t i = 0U; i < sizeof(outResult->rfTimedMode2ObservedTokens) /
                                  sizeof(outResult->rfTimedMode2ObservedTokens[0]);
         ++i) {
      outResult->rfTimedMode2ObservedTokens[i] =
          readLe32(params + 177U + (4U * i));
    }
  }
  if (completeEvent.returnParamsLen >= 236U) {
    outResult->rfTimingOwnerVersion = params[201];
    outResult->rfTimingOwnerFlags = params[202];
    outResult->rfTimingOwnerStatus = params[203];
    outResult->rfTimingOwnerActiveSubevent = params[204];
    outResult->rfTimingOwnerProcedureCounter = readLe16(params + 205U);
    outResult->rfTimingOwnerConnHandle = readLe16(params + 207U);
    outResult->rfTimingOwnerHeartbeat = readLe32(params + 209U);
    outResult->rfTimingOwnerNextProcedureHeartbeat = readLe32(params + 213U);
    outResult->rfTimingOwnerNextSubeventHeartbeat = readLe32(params + 217U);
    outResult->rfTimingOwnerProcedureIntervalTicks = readLe32(params + 221U);
    outResult->rfTimingOwnerSubeventDelayTicks = readLe32(params + 225U);
    outResult->rfTimingOwnerToken = readLe32(params + 229U);
    outResult->rfTimingOwnerPeerGapTicks = params[233];
    outResult->rfTimingOwnerIntervalSelector = params[234];
    outResult->rfTimingOwnerValid =
        outResult->rfTimingOwnerVersion == 1U &&
        ((outResult->rfTimingOwnerFlags & 0x01U) != 0U);
    outResult->rfTimingOwnerProcedureActive =
        (outResult->rfTimingOwnerFlags & 0x02U) != 0U;
    outResult->rfTimingOwnerProcedureIntervalComputed =
        (outResult->rfTimingOwnerFlags & 0x04U) != 0U;
    outResult->rfTimingOwnerSubeventDelayComputed =
        (outResult->rfTimingOwnerFlags & 0x08U) != 0U;
    outResult->rfTimingOwnerControllerSnapshot =
        (outResult->rfTimingOwnerFlags & 0x10U) != 0U;
    outResult->rfTimingOwnerConnectedSchedule =
        (outResult->rfTimingOwnerFlags & 0x20U) != 0U;
    outResult->rfTimingOwnerTokenValid =
        outResult->rfTimingOwnerToken != 0U;
  }
  if (completeEvent.returnParamsLen >= 250U) {
    outResult->rfPacketParamsVersion = params[236];
    outResult->rfPacketParamsFlags = params[237];
    outResult->rfPacketS0 = params[238];
    outResult->rfPacketCteInfo = params[239];
    outResult->rfPacketPayloadLen = params[240];
    outResult->rfPacketMagic0 = params[241];
    outResult->rfPacketMagic1 = params[242];
    outResult->rfPacketType = params[243];
    outResult->rfPacketSequence = params[244];
    outResult->rfPacketChannel = params[245];
    outResult->rfPacketControlToProbeDelayUs = readLe16(params + 246U);
    outResult->rfPacketResponseListenWindowUs = readLe16(params + 248U);
    outResult->rfPacketParamsValid =
        outResult->rfPacketParamsVersion == 1U &&
        ((outResult->rfPacketParamsFlags & 0x01U) != 0U);
    outResult->rfPacketParamsControllerOwned =
        (outResult->rfPacketParamsFlags & 0x02U) != 0U;
    outResult->rfPacketParamsCteInfoIncludesType =
        (outResult->rfPacketParamsFlags & 0x04U) != 0U;
  }
  return true;
}

bool parseVprToneSnapshotResponse(const uint8_t* packet,
                                  size_t packetLen,
                                  uint16_t expectedOpcode,
                                  BleCsVprToneSnapshotResult* outResult) {
  if (outResult == nullptr) {
    return false;
  }
  *outResult = BleCsVprToneSnapshotResult{};

  BleCsHciCommandCompleteEvent completeEvent{};
  if (!BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen,
                                                             &completeEvent) ||
      completeEvent.opcode != expectedOpcode ||
      completeEvent.returnParams == nullptr ||
      completeEvent.returnParamsLen < 32U) {
    return false;
  }

  const uint8_t* params = completeEvent.returnParams;
  outResult->valid = true;
  outResult->status = params[0];
  outResult->flags = params[1];
  outResult->version = params[2];
  outResult->snapshotValid =
      outResult->version == 1U && ((outResult->flags & 0x01U) != 0U);
  outResult->sampleNonZero = (outResult->flags & 0x02U) != 0U;
  outResult->radioDisabled = (outResult->flags & 0x04U) != 0U;
  outResult->toneConfigOk = (outResult->flags & 0x20U) != 0U;
  outResult->pct16 = readLe32(params + 4U);
  outResult->magPhase = readLe32(params + 8U);
  outResult->magStd = readLe32(params + 12U);
  outResult->frequency = readLe32(params + 16U);
  outResult->state = readLe32(params + 20U);
  outResult->cstonesEndEvent = readLe32(params + 24U);
  outResult->token = readLe32(params + 28U);
  outResult->tokenValid =
      outResult->token != 0U &&
      outResult->token ==
          buildMeasurementToneSnapshotToken(outResult->version,
                                            outResult->flags,
                                            outResult->status,
                                            outResult->pct16,
                                            outResult->magPhase,
                                            outResult->magStd,
                                            outResult->frequency,
                                            outResult->state,
                                            outResult->cstonesEndEvent);
  if (completeEvent.returnParamsLen >= 48U) {
    outResult->timedMode2Version = params[32];
    outResult->timedMode2Status = params[33];
    outResult->timedMode2Channel = params[34];
    outResult->timedMode2Flags = params[35];
    outResult->timedMode2Token = readLe32(params + 36U);
    outResult->timedMode2PacketS0 = params[40];
    outResult->timedMode2PacketLength = params[41];
    outResult->timedMode2PacketType = params[42];
    outResult->timedMode2PacketSequence = params[43];
    outResult->timedMode2PacketChannel = params[44];
    outResult->timedMode2RssiSample = params[45];
    outResult->timedMode2CrcStatus = params[46];
    outResult->timedMode2EventMask = params[47];
    outResult->timedMode2Valid =
        outResult->timedMode2Version == 1U &&
        ((outResult->timedMode2Flags & 0x01U) != 0U);
    outResult->timedMode2TokenValid = outResult->timedMode2Token != 0U;
  }
  return true;
}

bool BleCsControllerSession::begin(uint16_t connHandle,
                                   const BleCsControllerSessionConfig& config) {
  reset();
  config_ = config;
  return workflow_.begin(connHandle, config.workflow);
}

bool BleCsControllerSession::buildNextCommandPacket(uint8_t* outPacket,
                                                    size_t maxLen,
                                                    size_t* outLen) {
  BleCsHciCommand command{};
  if (!workflow_.buildNextCommand(&command)) {
    return false;
  }
  return BleChannelSoundingRadio::encodeHciCommandPacket(command, outPacket, maxLen, outLen);
}

bool BleCsControllerSession::consumeWorkflowEventPacket(const uint8_t* packet, size_t packetLen) {
  const bool ok = workflow_.consumeHciEventPacket(packet, packetLen);
  if (!ok) {
    return false;
  }
  state_.workflowReady = workflow_.ready();
  return true;
}

bool BleCsControllerSession::consumeWorkflowStreamBytes(const uint8_t* data, size_t len) {
  const bool ok = workflowDecoder_.pushBytes(data, len, onWorkflowPacket, this);
  state_.workflowIgnoredPackets = workflowDecoder_.ignoredPacketCount();
  state_.workflowIgnoredBytes = workflowDecoder_.ignoredByteCount();
  return ok;
}

bool BleCsControllerSession::consumeResultEventPacket(BleCsControllerResultSource source,
                                                      const uint8_t* packet,
                                                      size_t packetLen) {
  return consumeResultPacket(source, packet, packetLen);
}

bool BleCsControllerSession::consumeResultStreamBytes(BleCsControllerResultSource source,
                                                      const uint8_t* data,
                                                      size_t len) {
  BleHciPacketStreamDecoder& decoder =
      (source == BleCsControllerResultSource::kLocal) ? localDecoder_ : peerDecoder_;
  const bool ok = decoder
      .pushBytes(data, len,
                 (source == BleCsControllerResultSource::kLocal) ? onLocalResultPacket
                                                                 : onPeerResultPacket,
                 this);
  if (source == BleCsControllerResultSource::kLocal) {
    state_.localIgnoredPackets = decoder.ignoredPacketCount();
    state_.localIgnoredBytes = decoder.ignoredByteCount();
  } else {
    state_.peerIgnoredPackets = decoder.ignoredPacketCount();
    state_.peerIgnoredBytes = decoder.ignoredByteCount();
  }
  return ok;
}

bool BleCsControllerSession::ready() const { return workflow_.ready(); }

bool BleCsControllerSession::failed() const { return workflow_.failed(); }

bool BleCsControllerSession::estimateValid() const { return state_.estimateValid; }

bool BleCsControllerSession::refreshEstimateFromCompletedResults() {
  if (!completedLocalResult_.isComplete || !completedPeerResult_.isComplete ||
      completedLocalResult_.header.procedureDoneStatus != kBleCsProcedureDoneComplete ||
      completedPeerResult_.header.procedureDoneStatus != kBleCsProcedureDoneComplete ||
      completedLocalResult_.header.connHandle != completedPeerResult_.header.connHandle ||
      completedLocalResult_.header.configId != completedPeerResult_.header.configId ||
      completedLocalResult_.header.procedureCounter !=
          completedPeerResult_.header.procedureCounter ||
      completedLocalResult_.stepData == nullptr ||
      completedPeerResult_.stepData == nullptr ||
      completedLocalResult_.stepDataLen == 0U ||
      completedPeerResult_.stepDataLen == 0U) {
    return false;
  }

  BleCsEstimate estimate{};
  if (!BleChannelSoundingRadio::estimateDistanceFromSubeventResults(
          completedLocalResult_, completedPeerResult_,
          config_.localRoleIsInitiator, &estimate)) {
    return false;
  }

  state_.estimate = estimate;
  state_.estimateValid = estimate.valid;
  state_.completedProcedureCounter =
      completedLocalResult_.header.procedureCounter;
  state_.completedConfigId = completedLocalResult_.header.configId;
  return state_.estimateValid;
}

bool BleCsControllerSession::applyEstimateToCompletedResults(
    const BleCsEstimate& estimate) {
  if (!estimate.valid ||
      !isfinite(estimate.distanceMeters) ||
      !(estimate.distanceMeters > 0.0f) ||
      !completedLocalResult_.isComplete ||
      !completedPeerResult_.isComplete ||
      completedLocalResult_.header.procedureDoneStatus != kBleCsProcedureDoneComplete ||
      completedPeerResult_.header.procedureDoneStatus != kBleCsProcedureDoneComplete ||
      completedLocalResult_.header.connHandle != completedPeerResult_.header.connHandle ||
      completedLocalResult_.header.configId != completedPeerResult_.header.configId ||
      completedLocalResult_.header.procedureCounter !=
          completedPeerResult_.header.procedureCounter ||
      completedLocalResult_.stepData == nullptr ||
      completedPeerResult_.stepData == nullptr ||
      completedLocalResult_.stepDataLen == 0U ||
      completedPeerResult_.stepDataLen == 0U) {
    return false;
  }

  state_.estimate = estimate;
  state_.estimateValid = true;
  state_.completedProcedureCounter =
      completedLocalResult_.header.procedureCounter;
  state_.completedConfigId = completedLocalResult_.header.configId;
  return true;
}

const BleCsControllerSessionState& BleCsControllerSession::state() const { return state_; }

const BleCsControllerWorkflowState& BleCsControllerSession::workflowState() const {
  return workflow_.state();
}

uint8_t BleCsControllerSession::lastProcedureAbortReason() const {
  return lastProcedureAbortReason_;
}

uint8_t BleCsControllerSession::lastSubeventAbortReason() const {
  return lastSubeventAbortReason_;
}

const BleCsSubeventResult& BleCsControllerSession::localResult() const {
  return localResult_;
}

const BleCsSubeventResult& BleCsControllerSession::peerResult() const {
  return peerResult_;
}

const BleCsSubeventResult& BleCsControllerSession::completedLocalResult() const {
  return completedLocalResult_;
}

const BleCsSubeventResult& BleCsControllerSession::completedPeerResult() const {
  return completedPeerResult_;
}

void BleCsControllerSession::resetProcedureRunState() {
  localDecoder_.reset();
  peerDecoder_.reset();
  localDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  peerDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  localReassembler_.reset();
  peerReassembler_.reset();
  localResult_ = BleCsSubeventResult{};
  peerResult_ = BleCsSubeventResult{};
  state_.localResultComplete = false;
  state_.peerResultComplete = false;
  resetAccumulatedProcedureResults();
}

void BleCsControllerSession::reconcileReadyWorkflowShadow(
    uint8_t selectedConfigId, bool sessionOpen, bool configCreated,
    bool securityEnabled, bool procedureParametersApplied, bool procedureEnabled) {
  workflow_.reconcileReadyShadowState(selectedConfigId, sessionOpen, configCreated,
                                      securityEnabled, procedureParametersApplied,
                                      procedureEnabled);
  state_.workflowReady = workflow_.ready();
}

void BleCsControllerSession::resetAccumulatedProcedureResults() {
  resetAccumulatedProcedureResult(BleCsControllerResultSource::kLocal);
  resetAccumulatedProcedureResult(BleCsControllerResultSource::kPeer);
}

void BleCsControllerSession::resetAccumulatedProcedureResult(
    BleCsControllerResultSource source) {
  if (source == BleCsControllerResultSource::kLocal) {
    accumulatedLocalResult_ = BleCsSubeventResult{};
  } else {
    accumulatedPeerResult_ = BleCsSubeventResult{};
  }
}

bool BleCsControllerSession::accumulateProcedureResult(BleCsControllerResultSource source,
                                                       const BleCsSubeventResult& result) {
  if (!result.isComplete) {
    return false;
  }

  /* Reject aborted results.  The VPR may tag subevent results with a non-zero
   * abort reason (e.g. 0x06 = LL Procedure Timeout, 0x0B = Connection Terminated
   * by Local Host).  Accumulating aborted data would produce invalid distance
   * estimates later, so treat them as rejected and clear both sides of the
   * current procedure. */
  if (result.header.procedureAbortReason != 0U ||
      result.header.subeventAbortReason != 0U ||
      result.header.procedureDoneStatus == kBleCsProcedureDoneAborted ||
      result.header.subeventDoneStatus == kBleCsSubeventDoneAborted) {
    lastProcedureAbortReason_ = result.header.procedureAbortReason;
    lastSubeventAbortReason_ = result.header.subeventAbortReason;
    state_.estimateValid = false;
    resetAccumulatedProcedureResults();
    return false;
  }

  if (result.stepData == nullptr || result.stepDataLen == 0U) {
    return false;
  }

  BleCsSubeventResult& accumulated =
      (source == BleCsControllerResultSource::kLocal) ? accumulatedLocalResult_
                                                      : accumulatedPeerResult_;
  uint8_t* storage =
      (source == BleCsControllerResultSource::kLocal) ? accumulatedLocalStepData_
                                                      : accumulatedPeerStepData_;

  const bool sameProcedure =
      accumulated.stepData != nullptr &&
      accumulated.header.connHandle == result.header.connHandle &&
      accumulated.header.configId == result.header.configId &&
      accumulated.header.procedureCounter == result.header.procedureCounter &&
      accumulated.header.numAntennaPaths == result.header.numAntennaPaths;
  if (!sameProcedure) {
    accumulated = BleCsSubeventResult{};
  }

  const uint16_t previousSteps = accumulated.header.numStepsReported;
  const uint16_t previousBytes = accumulated.stepDataLen;
  if (static_cast<size_t>(previousBytes) + result.stepDataLen >
      kBleCsMaxControllerStepDataBytes) {
    accumulated = BleCsSubeventResult{};
    return false;
  }

  if (result.stepDataLen > 0U) {
    memcpy(storage + previousBytes, result.stepData, result.stepDataLen);
  }

  accumulated.header = result.header;
  accumulated.header.numStepsReported =
      static_cast<uint16_t>(previousSteps + result.header.numStepsReported);
  accumulated.stepData = storage;
  accumulated.stepDataLen =
      static_cast<uint16_t>(previousBytes + result.stepDataLen);
  accumulated.isContinuation = false;
  accumulated.isPartial =
      (result.header.procedureDoneStatus == kBleCsProcedureDonePartial);
  accumulated.isComplete = !accumulated.isPartial;
  return true;
}

bool BleCsControllerSession::onWorkflowPacket(const uint8_t* packet,
                                              size_t packetLen,
                                              void* userData) {
  BleCsControllerSession* session = static_cast<BleCsControllerSession*>(userData);
  return (session != nullptr) && session->consumeWorkflowEventPacket(packet, packetLen);
}

bool BleCsControllerSession::onLocalResultPacket(const uint8_t* packet,
                                                 size_t packetLen,
                                                 void* userData) {
  BleCsControllerSession* session = static_cast<BleCsControllerSession*>(userData);
  return (session != nullptr) &&
         session->consumeResultPacket(BleCsControllerResultSource::kLocal, packet, packetLen);
}

bool BleCsControllerSession::onPeerResultPacket(const uint8_t* packet,
                                                size_t packetLen,
                                                void* userData) {
  BleCsControllerSession* session = static_cast<BleCsControllerSession*>(userData);
  return (session != nullptr) &&
         session->consumeResultPacket(BleCsControllerResultSource::kPeer, packet, packetLen);
}

bool BleCsControllerSession::consumeResultPacket(BleCsControllerResultSource source,
                                                 const uint8_t* packet,
                                                 size_t packetLen) {
  BleCsHciLeMetaEvent metaEvent{};
  if (!BleChannelSoundingRadio::parseHciLeMetaEvent(packet, packetLen, &metaEvent)) {
    return false;
  }

  BleCsSubeventResultReassembler& reassembler =
      (source == BleCsControllerResultSource::kLocal) ? localReassembler_ : peerReassembler_;
  BleCsSubeventResult& result =
      (source == BleCsControllerResultSource::kLocal) ? localResult_ : peerResult_;

  bool ok = false;
  if (metaEvent.subeventCode == kBleCsHciEvtSubeventResult) {
    ok = reassembler.consumeInitialEvent(metaEvent.payload, metaEvent.payloadLen, &result);
  } else if (metaEvent.subeventCode == kBleCsHciEvtSubeventResultContinue) {
    ok = reassembler.consumeContinuationEvent(metaEvent.payload, metaEvent.payloadLen, &result);
  } else {
    return false;
  }
  if (!ok) {
    return false;
  }

  if (source == BleCsControllerResultSource::kLocal) {
    state_.localResultComplete = result.isComplete;
  } else {
    state_.peerResultComplete = result.isComplete;
  }
  if (result.isComplete) {
    if (!accumulateProcedureResult(source, result)) {
      return false;
    }
  }
  updateEstimateIfComplete();
  return true;
}

bool BleCsControllerSession::consumeCompletedResult(
    BleCsControllerResultSource source,
    const BleCsSubeventResult& result) {
  if (!result.isComplete) {
    return false;
  }
  if (!accumulateProcedureResult(source, result)) {
    return false;
  }

  const BleCsSubeventResult& accumulated =
      (source == BleCsControllerResultSource::kLocal) ? accumulatedLocalResult_
                                                      : accumulatedPeerResult_;
  if (source == BleCsControllerResultSource::kLocal) {
    localResult_ = accumulated;
    state_.localResultComplete = accumulated.isComplete;
  } else {
    peerResult_ = accumulated;
    state_.peerResultComplete = accumulated.isComplete;
  }

  updateEstimateIfComplete();
  return true;
}

bool BleCsControllerSession::snapshotCompletedResultPair(
    const BleCsSubeventResult& localResult,
    const BleCsSubeventResult& peerResult) {
  if (localResult.stepData == nullptr || peerResult.stepData == nullptr ||
      localResult.stepDataLen > sizeof(completedLocalStepData_) ||
      peerResult.stepDataLen > sizeof(completedPeerStepData_)) {
    return false;
  }

  completedLocalResult_ = localResult;
  completedPeerResult_ = peerResult;
  if (localResult.stepDataLen > 0U) {
    memcpy(completedLocalStepData_, localResult.stepData, localResult.stepDataLen);
  }
  if (peerResult.stepDataLen > 0U) {
    memcpy(completedPeerStepData_, peerResult.stepData, peerResult.stepDataLen);
  }
  completedLocalResult_.stepData =
      (localResult.stepDataLen > 0U) ? completedLocalStepData_ : nullptr;
  completedPeerResult_.stepData =
      (peerResult.stepDataLen > 0U) ? completedPeerStepData_ : nullptr;
  return true;
}

void BleCsControllerSession::updateEstimateIfComplete() {
  if (!accumulatedLocalResult_.isComplete || !accumulatedPeerResult_.isComplete) {
    return;
  }
  if (accumulatedLocalResult_.header.procedureDoneStatus != kBleCsProcedureDoneComplete ||
      accumulatedPeerResult_.header.procedureDoneStatus != kBleCsProcedureDoneComplete) {
    /* If either result indicates abort, clean up so stale data doesn't
     * contaminate the next procedure. */
    if (accumulatedLocalResult_.header.procedureDoneStatus == kBleCsProcedureDoneAborted ||
        accumulatedPeerResult_.header.procedureDoneStatus == kBleCsProcedureDoneAborted ||
        accumulatedLocalResult_.header.procedureAbortReason != 0U ||
        accumulatedPeerResult_.header.procedureAbortReason != 0U) {
      resetAccumulatedProcedureResults();
    }
    return;
  }
  if (accumulatedLocalResult_.header.connHandle != accumulatedPeerResult_.header.connHandle ||
      accumulatedLocalResult_.header.configId != accumulatedPeerResult_.header.configId ||
      accumulatedLocalResult_.header.procedureCounter !=
          accumulatedPeerResult_.header.procedureCounter) {
    return;
  }
  if (!snapshotCompletedResultPair(accumulatedLocalResult_, accumulatedPeerResult_)) {
    return;
  }
  BleCsEstimate estimate{};
  if (!BleChannelSoundingRadio::estimateDistanceFromSubeventResults(
      completedLocalResult_, completedPeerResult_, config_.localRoleIsInitiator,
      &estimate)) {
    return;
  }
  state_.estimate = estimate;
  state_.estimateValid = estimate.valid;
  state_.completedProcedureCounter = completedLocalResult_.header.procedureCounter;
  state_.completedConfigId = completedLocalResult_.header.configId;
  resetAccumulatedProcedureResults();
}

BleCsControllerHost::BleCsControllerHost()
    : config_{},
      state_{},
      session_{},
      controllerDecoder_{},
      localDecoder_{},
      peerDecoder_{},
      controllerPeerResultsExpected_{false} {}

void BleCsControllerHost::reset() {
  config_ = BleCsControllerHostConfig{};
  state_ = BleCsControllerHostState{};
  session_.reset();
  controllerDecoder_.reset();
  localDecoder_.reset();
  peerDecoder_.reset();
  controllerPeerResultsExpected_ = false;
  controllerDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  localDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  peerDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
}

bool BleCsControllerHost::begin(uint16_t connHandle, const BleCsControllerHostConfig& config) {
  reset();
  if (config.sendPacket == nullptr) {
    return false;
  }
  config_ = config;
  if (!session_.begin(connHandle, config.session)) {
    return false;
  }
  state_.began = true;
  return true;
}

bool BleCsControllerHost::pumpCommands(uint8_t maxCommands) {
  if (!state_.began || config_.sendPacket == nullptr || maxCommands == 0U) {
    return false;
  }

  uint8_t packet[80] = {0};
  size_t packetLen = 0U;
  for (uint8_t i = 0U; i < maxCommands; ++i) {
    if (!session_.buildNextCommandPacket(packet, sizeof(packet), &packetLen)) {
      break;
    }
    const uint16_t opcode = (packetLen >= 3U) ? readLe16(packet + 1U) : 0U;
    switch (opcode) {
      case kBleCsHciOpCreateConfig:
      case kBleCsHciOpRemoveConfig:
      case kBleCsHciOpSetProcedureParameters:
      case kBleCsHciOpProcedureEnable:
        resetProcedureRunState();
        break;
      default:
        break;
    }
    if (!config_.sendPacket(packet, packetLen, config_.userData)) {
      return false;
    }
    ++state_.sentCommandPackets;
    state_.sentCommandBytes =
        static_cast<uint32_t>(state_.sentCommandBytes + packetLen);
    state_.lastCommandOpcode = opcode;
  }
  return true;
}

void BleCsControllerHost::resetProcedureRunState() {
  session_.resetProcedureRunState();
  controllerPeerResultsExpected_ = false;
}

void BleCsControllerHost::reconcileReadyWorkflowShadow(
    uint8_t selectedConfigId, bool sessionOpen, bool configCreated,
    bool securityEnabled, bool procedureParametersApplied, bool procedureEnabled) {
  session_.reconcileReadyWorkflowShadow(selectedConfigId, sessionOpen, configCreated,
                                        securityEnabled, procedureParametersApplied,
                                        procedureEnabled);
}

bool BleCsControllerHost::consumeIngressPacket(BleCsControllerIngressSource source,
                                               const uint8_t* packet,
                                               size_t packetLen) {
  if (packet == nullptr || packetLen == 0U) {
    return false;
  }

  if (source == BleCsControllerIngressSource::kController) {
    uint8_t eventCode = 0U;
    const uint8_t* eventParams = nullptr;
    size_t eventParamsLen = 0U;
    if (decodeHciEventFrame(packet, packetLen, &eventCode, &eventParams, &eventParamsLen) &&
        eventCode == kBleHciEvtVendor && eventParams != nullptr && eventParamsLen > 0U) {
      if (eventParams[0U] == kBleCsVprVendorEvtPeerResultTrigger) {
        ++state_.vendorPeerResultTriggers;
        ++state_.controllerEventPackets;
        return true;
      }
      if (eventParams[0U] == kBleCsVprVendorEvtPeerResultSource) {
        controllerPeerResultsExpected_ = true;
        ++state_.controllerPeerResultMarkers;
        ++state_.controllerEventPackets;
        return true;
      }
    }

    BleCsHciLeMetaEvent metaEvent{};
    if (BleChannelSoundingRadio::parseHciLeMetaEvent(packet, packetLen, &metaEvent) &&
        (metaEvent.subeventCode == kBleCsHciEvtSubeventResult ||
         metaEvent.subeventCode == kBleCsHciEvtSubeventResultContinue)) {
      BleCsSubeventResult parsedResult{};
      bool parsedResultValid = false;
      if (metaEvent.subeventCode == kBleCsHciEvtSubeventResult) {
        parsedResultValid = BleChannelSoundingRadio::parseHciSubeventResultEvent(
            metaEvent.payload, metaEvent.payloadLen, &parsedResult);
      } else {
        parsedResultValid = BleChannelSoundingRadio::parseHciSubeventResultContinueEvent(
            metaEvent.payload, metaEvent.payloadLen, &parsedResult);
      }
      const BleCsControllerResultSource resultSource =
          controllerPeerResultsExpected_ ? BleCsControllerResultSource::kPeer
                                         : BleCsControllerResultSource::kLocal;
      const bool isInitialSubevent =
          parsedResultValid && metaEvent.subeventCode == kBleCsHciEvtSubeventResult;
      if (parsedResultValid && resultSource == BleCsControllerResultSource::kLocal &&
          metaEvent.subeventCode == kBleCsHciEvtSubeventResult) {
        state_.vendorPeerResultConfigId = parsedResult.header.configId;
        state_.vendorPeerResultProcedureCounter = parsedResult.header.procedureCounter;
      }
      const bool ok = session_.consumeResultEventPacket(resultSource, packet, packetLen);
      if (ok) {
        if (resultSource == BleCsControllerResultSource::kLocal) {
          ++state_.localResultPackets;
          if (isInitialSubevent) {
            ++state_.localSubeventResults;
          }
        } else {
          ++state_.peerResultPackets;
          if (isInitialSubevent) {
            ++state_.peerSubeventResults;
          }
          if (parsedResultValid && parsedResult.isComplete) {
            controllerPeerResultsExpected_ = false;
          }
        }
      }
      return ok;
    }

    const bool ok = session_.consumeWorkflowEventPacket(packet, packetLen);
    if (ok) {
      ++state_.controllerEventPackets;
      return true;
    }

    if (session_.ready()) {
      BleCsHciCommandStatusEvent statusEvent{};
      if (BleChannelSoundingRadio::parseHciCommandStatusEvent(packet, packetLen, &statusEvent)) {
        switch (statusEvent.opcode) {
          case kBleCsHciOpReadRemoteSupportedCapabilities:
          case kBleCsHciOpSecurityEnable:
          case kBleCsHciOpSetDefaultSettings:
          case kBleCsHciOpCreateConfig:
          case kBleCsHciOpRemoveConfig:
          case kBleCsHciOpSetProcedureParameters:
          case kBleCsHciOpProcedureEnable:
            ++state_.controllerEventPackets;
            return true;
          default:
            break;
        }
      }

      BleCsHciCommandCompleteEvent completeEvent{};
      if (BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen,
                                                                &completeEvent)) {
        switch (completeEvent.opcode) {
          case kBleCsHciOpReadRemoteSupportedCapabilities:
          case kBleCsHciOpSecurityEnable:
          case kBleCsHciOpSetDefaultSettings:
          case kBleCsHciOpCreateConfig:
          case kBleCsHciOpRemoveConfig:
          case kBleCsHciOpSetProcedureParameters:
          case kBleCsHciOpProcedureEnable:
            ++state_.controllerEventPackets;
            return true;
          default:
            break;
        }
      }

      BleCsHciLeMetaEvent ignoredMetaEvent{};
      if (BleChannelSoundingRadio::parseHciLeMetaEvent(packet, packetLen, &ignoredMetaEvent)) {
        switch (ignoredMetaEvent.subeventCode) {
          case kBleCsHciEvtReadRemoteSupportedCapabilitiesComplete:
          case kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2:
          case kBleCsHciEvtConfigComplete:
          case kBleCsHciEvtSecurityEnableComplete:
          case kBleCsHciEvtProcedureEnableComplete:
            ++state_.controllerEventPackets;
            return true;
          default:
            break;
        }
      }
    }

    return false;
  }

  const BleCsControllerResultSource resultSource =
      (source == BleCsControllerIngressSource::kPeerResult)
          ? BleCsControllerResultSource::kPeer
          : BleCsControllerResultSource::kLocal;
  BleCsHciLeMetaEvent metaEvent{};
  const bool isInitialSubevent =
      BleChannelSoundingRadio::parseHciLeMetaEvent(packet, packetLen, &metaEvent) &&
      metaEvent.subeventCode == kBleCsHciEvtSubeventResult;
  const bool ok = session_.consumeResultEventPacket(resultSource, packet, packetLen);
  if (ok) {
    if (resultSource == BleCsControllerResultSource::kLocal) {
      ++state_.localResultPackets;
      if (isInitialSubevent) {
        ++state_.localSubeventResults;
      }
    } else {
      ++state_.peerResultPackets;
      if (isInitialSubevent) {
        ++state_.peerSubeventResults;
      }
    }
  }
  return ok;
}

bool BleCsControllerHost::consumeCompletedResult(
    BleCsControllerResultSource source,
    const BleCsSubeventResult& result) {
  const bool ok = session_.consumeCompletedResult(source, result);
  if (ok) {
    if (source == BleCsControllerResultSource::kLocal) {
      ++state_.localSubeventResults;
    } else {
      ++state_.peerSubeventResults;
      controllerPeerResultsExpected_ = false;
    }
  }
  return ok;
}

bool BleCsControllerHost::consumeResultEventStream(
    BleCsControllerResultSource source,
    const BleCsSubeventResult& result) {
  if (!result.isComplete ||
      (result.stepDataLen > 0U && result.stepData == nullptr)) {
    return false;
  }

  const BleCsControllerIngressSource ingressSource =
      (source == BleCsControllerResultSource::kPeer)
          ? BleCsControllerIngressSource::kPeerResult
          : BleCsControllerIngressSource::kLocalResult;
  uint8_t packet[260] = {0};
  size_t packetLen = 0U;
  size_t offset = 0U;
  bool emitted = false;

  while (true) {
    BleCsSubeventResultFragment fragment{};
    if (!BleChannelSoundingRadio::buildH4LeMetaSubeventResultFragmentPacket(
            result, offset, packet, sizeof(packet), &packetLen, &fragment)) {
      return false;
    }
    if (!consumeIngressPacket(ingressSource, packet, packetLen)) {
      return false;
    }
    emitted = true;
    offset = fragment.nextStepDataOffset;
    if (!fragment.more) {
      break;
    }
    if (offset >= result.stepDataLen) {
      return false;
    }
  }

  return emitted;
}

bool BleCsControllerHost::consumeMode2ResultsFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    const BleCsSubeventResultHeader& headerTemplate,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen) {
  BleCsSubeventResult localResult{};
  BleCsSubeventResult peerResult{};
  if (!BleChannelSoundingRadio::buildMode2SubeventResultFromMeasurements(
          measurements, count, false, headerTemplate, localStepData,
          localMaxStepDataLen, &localResult) ||
      !BleChannelSoundingRadio::buildMode2SubeventResultFromMeasurements(
          measurements, count, true, headerTemplate, peerStepData,
          peerMaxStepDataLen, &peerResult)) {
    return false;
  }

  if (!consumeCompletedResult(BleCsControllerResultSource::kLocal, localResult)) {
    return false;
  }
  if (!consumeCompletedResult(BleCsControllerResultSource::kPeer, peerResult)) {
    resetProcedureRunState();
    return false;
  }
  return true;
}

bool BleCsControllerHost::consumeMode2ResultEventsFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    const BleCsSubeventResultHeader& headerTemplate,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen) {
  BleCsSubeventResult localResult{};
  BleCsSubeventResult peerResult{};
  if (!BleChannelSoundingRadio::buildMode2SubeventResultFromMeasurements(
          measurements, count, false, headerTemplate, localStepData,
          localMaxStepDataLen, &localResult) ||
      !BleChannelSoundingRadio::buildMode2SubeventResultFromMeasurements(
          measurements, count, true, headerTemplate, peerStepData,
          peerMaxStepDataLen, &peerResult)) {
    return false;
  }

  if (!consumeResultEventStream(BleCsControllerResultSource::kLocal, localResult)) {
    return false;
  }
  if (!consumeResultEventStream(BleCsControllerResultSource::kPeer, peerResult)) {
    resetProcedureRunState();
    return false;
  }
  return true;
}

bool BleCsControllerHost::consumeMode2ControllerEventsFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    const BleCsSubeventResultHeader& headerTemplate,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen) {
  BleCsSubeventResult localResult{};
  BleCsSubeventResult peerResult{};
  if (!BleChannelSoundingRadio::buildMode2SubeventResultFromMeasurements(
          measurements, count, false, headerTemplate, localStepData,
          localMaxStepDataLen, &localResult) ||
      !BleChannelSoundingRadio::buildMode2SubeventResultFromMeasurements(
          measurements, count, true, headerTemplate, peerStepData,
          peerMaxStepDataLen, &peerResult)) {
    return false;
  }

  const auto emitResult = [this](BleCsControllerResultSource source,
                                 const BleCsSubeventResult& result) -> bool {
    if (!result.isComplete ||
        (result.stepDataLen > 0U && result.stepData == nullptr)) {
      return false;
    }

    uint8_t packet[260] = {0};
    size_t packetLen = 0U;
    if (source == BleCsControllerResultSource::kPeer) {
      if (!buildH4VendorPeerResultSourceEvent(
              packet, sizeof(packet), result.header.configId,
              result.header.procedureCounter, &packetLen) ||
          !consumeIngressPacket(BleCsControllerIngressSource::kController,
                                packet, packetLen)) {
        return false;
      }
    }

    size_t offset = 0U;
    bool emitted = false;
    while (true) {
      BleCsSubeventResultFragment fragment{};
      if (!BleChannelSoundingRadio::buildH4LeMetaSubeventResultFragmentPacket(
              result, offset, packet, sizeof(packet), &packetLen, &fragment) ||
          !consumeIngressPacket(BleCsControllerIngressSource::kController,
                                packet, packetLen)) {
        return false;
      }
      emitted = true;
      offset = fragment.nextStepDataOffset;
      if (!fragment.more) {
        break;
      }
      if (offset >= result.stepDataLen) {
        return false;
      }
    }
    return emitted;
  };

  if (!emitResult(BleCsControllerResultSource::kLocal, localResult)) {
    return false;
  }
  if (!emitResult(BleCsControllerResultSource::kPeer, peerResult)) {
    resetProcedureRunState();
    return false;
  }
  return true;
}

bool BleCsControllerHost::consumeIngressBytes(BleCsControllerIngressSource source,
                                              const uint8_t* data,
                                              size_t len) {
  if (source == BleCsControllerIngressSource::kController) {
    const bool ok = controllerDecoder_.pushBytes(data, len, onControllerPacket, this);
    state_.controllerIgnoredPackets = controllerDecoder_.ignoredPacketCount();
    state_.controllerIgnoredBytes = controllerDecoder_.ignoredByteCount();
    return ok;
  }

  BleHciPacketStreamDecoder& decoder =
      (source == BleCsControllerIngressSource::kPeerResult) ? peerDecoder_ : localDecoder_;
  const bool ok = decoder.pushBytes(
      data, len,
      (source == BleCsControllerIngressSource::kPeerResult) ? onPeerPacket : onLocalPacket,
      this);
  return ok;
}

bool BleCsControllerHost::ready() const { return session_.ready(); }

bool BleCsControllerHost::failed() const { return session_.failed(); }

bool BleCsControllerHost::estimateValid() const { return session_.estimateValid(); }

bool BleCsControllerHost::refreshEstimateFromCompletedResults() {
  return session_.refreshEstimateFromCompletedResults();
}

bool BleCsControllerHost::applyEstimateToCompletedResults(
    const BleCsEstimate& estimate) {
  return session_.applyEstimateToCompletedResults(estimate);
}

uint8_t BleCsControllerHost::lastProcedureAbortReason() const {
  return session_.lastProcedureAbortReason();
}

uint8_t BleCsControllerHost::lastSubeventAbortReason() const {
  return session_.lastSubeventAbortReason();
}

const BleCsControllerHostState& BleCsControllerHost::state() const { return state_; }

const BleCsControllerSessionState& BleCsControllerHost::sessionState() const {
  return session_.state();
}

const BleCsControllerWorkflowState& BleCsControllerHost::workflowState() const {
  return session_.workflowState();
}

const BleCsSubeventResult& BleCsControllerHost::localResult() const {
  return session_.localResult();
}

const BleCsSubeventResult& BleCsControllerHost::peerResult() const {
  return session_.peerResult();
}

const BleCsSubeventResult& BleCsControllerHost::completedLocalResult() const {
  return session_.completedLocalResult();
}

const BleCsSubeventResult& BleCsControllerHost::completedPeerResult() const {
  return session_.completedPeerResult();
}

bool BleCsControllerHost::onControllerPacket(const uint8_t* packet,
                                             size_t packetLen,
                                             void* userData) {
  BleCsControllerHost* host = static_cast<BleCsControllerHost*>(userData);
  return (host != nullptr) &&
         host->consumeIngressPacket(BleCsControllerIngressSource::kController, packet,
                                    packetLen);
}

bool BleCsControllerHost::onLocalPacket(const uint8_t* packet,
                                        size_t packetLen,
                                        void* userData) {
  BleCsControllerHost* host = static_cast<BleCsControllerHost*>(userData);
  return (host != nullptr) &&
         host->consumeIngressPacket(BleCsControllerIngressSource::kLocalResult, packet,
                                    packetLen);
}

bool BleCsControllerHost::onPeerPacket(const uint8_t* packet,
                                       size_t packetLen,
                                       void* userData) {
  BleCsControllerHost* host = static_cast<BleCsControllerHost*>(userData);
  return (host != nullptr) &&
         host->consumeIngressPacket(BleCsControllerIngressSource::kPeerResult, packet,
                                    packetLen);
}

BleCsControllerStreamHost::BleCsControllerStreamHost()
    : config_{}, state_{}, host_{} {}

void BleCsControllerStreamHost::reset() {
  config_ = BleCsControllerStreamHostConfig{};
  state_ = BleCsControllerStreamHostState{};
  host_.reset();
}

bool BleCsControllerStreamHost::begin(uint16_t connHandle,
                                      const BleCsControllerStreamHostConfig& config) {
  reset();
  if (config.controllerStream == nullptr) {
    return false;
  }
  config_ = config;
  config_.maxCommandsPerPump = (config_.maxCommandsPerPump == 0U) ? 1U : config_.maxCommandsPerPump;
  config_.maxControllerBytesPerPoll = clampPollBytes(config_.maxControllerBytesPerPoll);
  config_.maxPeerBytesPerPoll = clampPollBytes(config_.maxPeerBytesPerPoll);

  BleCsControllerHostConfig hostConfig{};
  hostConfig.session = config_.session;
  hostConfig.sendPacket = onSendPacket;
  hostConfig.userData = this;
  return host_.begin(connHandle, hostConfig);
}

bool BleCsControllerStreamHost::pumpCommands() {
  return host_.pumpCommands(config_.maxCommandsPerPump);
}

bool BleCsControllerStreamHost::pollController() {
  if (config_.controllerStream == nullptr) {
    return false;
  }

  uint8_t bytes[128] = {0};
  const size_t limit = (config_.maxControllerBytesPerPoll < sizeof(bytes))
                           ? config_.maxControllerBytesPerPoll
                           : sizeof(bytes);
  size_t count = 0U;
  while (count < limit && config_.controllerStream->available() > 0) {
    const int raw = config_.controllerStream->read();
    if (raw < 0) {
      break;
    }
    bytes[count++] = static_cast<uint8_t>(raw);
  }
  if (count == 0U) {
    return true;
  }
  state_.controllerBytesRead = static_cast<uint32_t>(state_.controllerBytesRead + count);
  return host_.consumeIngressBytes(BleCsControllerIngressSource::kController, bytes, count);
}

bool BleCsControllerStreamHost::pollPeerResults() {
  if (config_.peerResultStream == nullptr) {
    return true;
  }

  uint8_t bytes[128] = {0};
  const size_t limit =
      (config_.maxPeerBytesPerPoll < sizeof(bytes)) ? config_.maxPeerBytesPerPoll : sizeof(bytes);
  size_t count = 0U;
  while (count < limit && config_.peerResultStream->available() > 0) {
    const int raw = config_.peerResultStream->read();
    if (raw < 0) {
      break;
    }
    bytes[count++] = static_cast<uint8_t>(raw);
  }
  if (count == 0U) {
    return true;
  }
  state_.peerBytesRead = static_cast<uint32_t>(state_.peerBytesRead + count);
  return host_.consumeIngressBytes(BleCsControllerIngressSource::kPeerResult, bytes, count);
}

bool BleCsControllerStreamHost::consumeControllerPacket(const uint8_t* packet, size_t packetLen) {
  if (packet == nullptr || packetLen == 0U) {
    return false;
  }
  state_.controllerBytesRead = static_cast<uint32_t>(state_.controllerBytesRead + packetLen);
  return host_.consumeIngressPacket(BleCsControllerIngressSource::kController, packet, packetLen);
}

bool BleCsControllerStreamHost::consumePeerPacket(const uint8_t* packet, size_t packetLen) {
  if (packet == nullptr || packetLen == 0U) {
    return false;
  }
  state_.peerBytesRead = static_cast<uint32_t>(state_.peerBytesRead + packetLen);
  return host_.consumeIngressPacket(BleCsControllerIngressSource::kPeerResult, packet, packetLen);
}

bool BleCsControllerStreamHost::consumeCompletedResult(
    BleCsControllerResultSource source,
    const BleCsSubeventResult& result) {
  return host_.consumeCompletedResult(source, result);
}

bool BleCsControllerStreamHost::consumeMode2ResultsFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    const BleCsSubeventResultHeader& headerTemplate,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen) {
  return host_.consumeMode2ResultsFromMeasurements(
      measurements, count, headerTemplate, localStepData, localMaxStepDataLen,
      peerStepData, peerMaxStepDataLen);
}

bool BleCsControllerStreamHost::consumeMode2ResultEventsFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    const BleCsSubeventResultHeader& headerTemplate,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen) {
  return host_.consumeMode2ResultEventsFromMeasurements(
      measurements, count, headerTemplate, localStepData, localMaxStepDataLen,
      peerStepData, peerMaxStepDataLen);
}

bool BleCsControllerStreamHost::consumeMode2ControllerEventsFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    const BleCsSubeventResultHeader& headerTemplate,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen) {
  return host_.consumeMode2ControllerEventsFromMeasurements(
      measurements, count, headerTemplate, localStepData, localMaxStepDataLen,
      peerStepData, peerMaxStepDataLen);
}

bool BleCsControllerStreamHost::poll() {
  return pollController() && pollPeerResults();
}

bool BleCsControllerStreamHost::loopOnce() {
  return pumpCommands() && poll();
}

bool BleCsControllerStreamHost::ready() const { return host_.ready(); }

bool BleCsControllerStreamHost::failed() const { return host_.failed(); }

bool BleCsControllerStreamHost::estimateValid() const { return host_.estimateValid(); }

bool BleCsControllerStreamHost::refreshEstimateFromCompletedResults() {
  return host_.refreshEstimateFromCompletedResults();
}

bool BleCsControllerStreamHost::applyEstimateToCompletedResults(
    const BleCsEstimate& estimate) {
  return host_.applyEstimateToCompletedResults(estimate);
}

uint8_t BleCsControllerStreamHost::lastProcedureAbortReason() const {
  return host_.lastProcedureAbortReason();
}

uint8_t BleCsControllerStreamHost::lastSubeventAbortReason() const {
  return host_.lastSubeventAbortReason();
}

const BleCsControllerStreamHostState& BleCsControllerStreamHost::state() const { return state_; }

const BleCsControllerHostState& BleCsControllerStreamHost::hostState() const {
  return host_.state();
}

const BleCsControllerSessionState& BleCsControllerStreamHost::sessionState() const {
  return host_.sessionState();
}

const BleCsControllerWorkflowState& BleCsControllerStreamHost::workflowState() const {
  return host_.workflowState();
}

const BleCsSubeventResult& BleCsControllerStreamHost::localResult() const {
  return host_.localResult();
}

const BleCsSubeventResult& BleCsControllerStreamHost::peerResult() const {
  return host_.peerResult();
}

const BleCsSubeventResult& BleCsControllerStreamHost::completedLocalResult() const {
  return host_.completedLocalResult();
}

const BleCsSubeventResult& BleCsControllerStreamHost::completedPeerResult() const {
  return host_.completedPeerResult();
}

void BleCsControllerStreamHost::resetProcedureRunState() {
  host_.resetProcedureRunState();
}

void BleCsControllerStreamHost::reconcileReadyWorkflowShadow(
    uint8_t selectedConfigId, bool sessionOpen, bool configCreated,
    bool securityEnabled, bool procedureParametersApplied, bool procedureEnabled) {
  host_.reconcileReadyWorkflowShadow(selectedConfigId, sessionOpen, configCreated,
                                     securityEnabled, procedureParametersApplied,
                                     procedureEnabled);
}

bool BleCsControllerStreamHost::onSendPacket(const uint8_t* packet,
                                             size_t packetLen,
                                             void* userData) {
  BleCsControllerStreamHost* transport = static_cast<BleCsControllerStreamHost*>(userData);
  if (transport == nullptr || transport->config_.controllerStream == nullptr || packet == nullptr ||
      packetLen == 0U) {
    return false;
  }

  const size_t written = transport->config_.controllerStream->write(packet, packetLen);
  if (written != packetLen) {
    transport->state_.lastWriteError = 1U;
    return false;
  }
  ++transport->state_.controllerPacketsWritten;
  transport->state_.controllerBytesWritten =
      static_cast<uint32_t>(transport->state_.controllerBytesWritten + written);
  return true;
}

size_t BleCsControllerStreamHost::clampPollBytes(size_t value) {
  if (value == 0U) {
    return 128U;
  }
  return (value > 1024U) ? 1024U : value;
}

void BleCsControllerVprHost::fillDemoConfig(BleCsControllerVprHostConfig* outConfig) {
  if (outConfig == nullptr) {
    return;
  }

  *outConfig = BleCsControllerVprHostConfig{};
  outConfig->maxCommandsPerPump = 1U;
  outConfig->maxControllerBytesPerPoll = 128U;
  outConfig->maxPeerBytesPerPoll = 128U;

  outConfig->builtInPeerDemo.enabled = true;
  outConfig->builtInPeerDemo.distanceMeters = 0.75f;
  outConfig->builtInPeerDemo.amplitude = 1024.0f;
  outConfig->builtInPeerDemo.channels[0] = 0U;
  outConfig->builtInPeerDemo.channels[1] = 12U;
  outConfig->builtInPeerDemo.channels[2] = 24U;
  outConfig->builtInPeerDemo.channels[3] = 36U;
  outConfig->builtInPeerDemo.channelCount = 4U;

  outConfig->session.localRoleIsInitiator = true;
  outConfig->session.workflow.applyDefaultSettings = true;
  outConfig->session.workflow.requireSecurityEnable = true;
  outConfig->session.workflow.defaultSettings.enableInitiatorRole = true;
  outConfig->session.workflow.defaultSettings.enableReflectorRole = true;
  outConfig->session.workflow.defaultSettings.csSyncAntennaSelection = 0xFEU;
  outConfig->session.workflow.defaultSettings.maxTxPowerDbm = -8;

  outConfig->session.workflow.createConfig.configId = 1U;
  outConfig->session.workflow.createConfig.createContext = 1U;
  outConfig->session.workflow.createConfig.mainModeType = kBleCsMainMode2;
  outConfig->session.workflow.createConfig.subModeType = 0xFFU;
  outConfig->session.workflow.createConfig.minMainModeSteps = 3U;
  outConfig->session.workflow.createConfig.maxMainModeSteps = 5U;
  outConfig->session.workflow.createConfig.mainModeRepetition = 1U;
  outConfig->session.workflow.createConfig.mode0Steps = 1U;
  outConfig->session.workflow.createConfig.role = 0U;
  outConfig->session.workflow.createConfig.rttType = 1U;
  outConfig->session.workflow.createConfig.csSyncPhy = 2U;
  BleChannelSoundingRadio::fillValidChannelMap(
      outConfig->session.workflow.createConfig.channelMap);
  outConfig->session.workflow.createConfig.channelMapRepetition = 1U;
  outConfig->session.workflow.createConfig.channelSelectionType = 1U;
  outConfig->session.workflow.createConfig.ch3cShape = 1U;
  outConfig->session.workflow.createConfig.ch3cJump = 3U;
  outConfig->session.workflow.createConfig.csEnhancements1 = 0x01U;

  outConfig->session.workflow.procedureParameters.configId = 1U;
  outConfig->session.workflow.procedureParameters.maxProcedureLen = 12U;
  outConfig->session.workflow.procedureParameters.minProcedureInterval = 200U;
  outConfig->session.workflow.procedureParameters.maxProcedureInterval = 300U;
  outConfig->session.workflow.procedureParameters.maxProcedureCount = 8U;
  outConfig->session.workflow.procedureParameters.minSubeventLen = 0x000456UL;
  outConfig->session.workflow.procedureParameters.maxSubeventLen = 0x000678UL;
  outConfig->session.workflow.procedureParameters.toneAntennaConfigSelection = 2U;
  outConfig->session.workflow.procedureParameters.phy = 2U;
  outConfig->session.workflow.procedureParameters.txPowerDelta = -6;
  outConfig->session.workflow.procedureParameters.preferredPeerAntenna = 0xFFU;
  outConfig->session.workflow.procedureParameters.snrControlInitiator = 0U;
  outConfig->session.workflow.procedureParameters.snrControlReflector = 0U;

  outConfig->session.workflow.procedureEnable.configId = 1U;
  outConfig->session.workflow.procedureEnable.enable = 1U;
}

BleCsControllerVprHost::BleCsControllerVprHost()
    : config_{},
      vprState_{},
      lastDrainStats_{},
      transport_{},
      host_{},
      lastRemoteFaeTable_{},
      lastTestEndComplete_{},
      lastTestEndCompleteValid_(false),
      cachedRemoteCapabilitiesV1_{},
      cachedRemoteCapabilitiesV2_{},
      cachedRemoteCapabilitiesV1Valid_(false),
      cachedRemoteCapabilitiesV2Valid_(false),
      lastTestResult_{},
      lastTestResultValid_(false),
      testResultCount_(0U),
      llControlBridgeQueuedStageValid_(false),
      llControlBridgeQueuedStage_(kBleCsVprPeerStageIdle),
      llControlBridgeNoTxStageValid_(false),
      llControlBridgeNoTxStage_(kBleCsVprPeerStageIdle),
      llControlBridgeNoTxPollSkips_(0U) {}

void BleCsControllerVprHost::reset() {
  config_ = BleCsControllerVprHostConfig{};
  vprState_ = BleCsControllerVprHostState{};
  lastDrainStats_ = BleCsControllerVprDrainStats{};
  host_.reset();
  lastRemoteFaeTable_ = BleCsFaeTable{};
  lastTestEndComplete_ = BleCsTestEndComplete{};
  lastTestEndCompleteValid_ = false;
  cachedRemoteCapabilitiesV1_ = BleCsControllerCapabilities{};
  cachedRemoteCapabilitiesV2_ = BleCsControllerCapabilities{};
  cachedRemoteCapabilitiesV1Valid_ = false;
  cachedRemoteCapabilitiesV2Valid_ = false;
  testReassembler_.reset();
  lastTestResult_ = BleCsSubeventResult{};
  lastTestResultValid_ = false;
  testResultCount_ = 0U;
  resetLlControlBridgeQueueState();
}

bool BleCsControllerVprHost::resetTransport(bool clearScripts) {
  /* Stop the VPR first so it cannot write new data into shared memory
   * between the clear and the read — otherwise the VPR's main loop would
   * overwrite the zeroed reserved field with a stale session-open bit and
   * syncVprState would never see the 1→0 transition. */
  transport_.stop();
  const bool ok = transport_.resetSharedState(clearScripts);
  /* syncVprState should detect the session-open→closed transition and call
   * handleDisconnect() → host_.reset() + workflow cleanup.  However the
   * nRF54L15's write-back data cache coherency bug prevents this: the memsets
   * in resetSharedState create dirty cache lines in the write-back cache, and
   * each transport getter calls invalidateCpuSystemCache() before reading,
   * which discards the dirty zeroes and reads stale VPR data from SRAM
   * (linkSessionOpen is still seen as true, so handleDisconnect is skipped).
   *
   * The Cache HAL at 0x4004B000 claims a DCLEANALL register but writing to it
   * causes a board hang (not implemented on this hardware).  The NRF_CACHE
   * ENABLE register at 0xE0082404 similarly appears to be locked from
   * non-secure writes.  With no working cache-clean primitive available, we
   * force the disconnect cleanup here whenever resetTransport is called —
   * the transport shared memory has been zeroed and the VPR stopped, so it is
   * logically disconnected regardless of what the cache-coherent read returns.
   *
   * This is equivalent to what handleDisconnect() does after its
   * refreshLinkSession + linkSessionOpen guard. */
  syncVprState();
  host_.reset();
  testReassembler_.reset();
  lastTestResultValid_ = false;
  testResultCount_ = 0U;
  resetLlControlBridgeQueueState();
  vprState_ = BleCsControllerVprHostState{};
  return ok;
}

bool BleCsControllerVprHost::addScriptResponse(uint16_t opcode,
                                               const uint8_t* response,
                                               size_t len) {
  const bool ok = transport_.addScriptResponse(opcode, response, len);
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::loadDefaultTransportImage() {
  const bool ok = transport_.loadDefaultCsControllerStubImage();
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::bootTransport(uint32_t readySpinLimit) {
  if (!transport_.bootLoadedFirmware()) {
    syncVprState();
    return false;
  }
  const bool ok = transport_.waitReady(readySpinLimit);
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::refreshLinkSession() {
  syncVprState();
  return vprState_.running && vprState_.transportStatus != 0U;
}

bool BleCsControllerVprHost::handleDisconnect() {
  refreshLinkSession();
  if (vprState_.linkSessionOpen) {
    return false;
  }
  host_.reset();
  testReassembler_.reset();
  lastTestResultValid_ = false;
  testResultCount_ = 0U;
  lastRemoteFaeTable_ = BleCsFaeTable{};
  lastTestEndComplete_ = BleCsTestEndComplete{};
  lastTestEndCompleteValid_ = false;
  cachedRemoteCapabilitiesV1_ = BleCsControllerCapabilities{};
  cachedRemoteCapabilitiesV2_ = BleCsControllerCapabilities{};
  cachedRemoteCapabilitiesV1Valid_ = false;
  cachedRemoteCapabilitiesV2Valid_ = false;
  resetLlControlBridgeQueueState();
  return true;
}

bool BleCsControllerVprHost::beginHost(uint16_t connHandle,
                                       const BleCsControllerVprHostConfig& config) {
  config_ = config;
  resetLlControlBridgeQueueState();

  volatile Nrf54l15VprTransportHostShared* sharedHost =
      nrf54l15_vpr_transport_host_shared();
  if (sharedHost != nullptr) {
    uint32_t packedDemoChannels = 0U;
    if (config.builtInPeerDemo.enabled && config.builtInPeerDemo.channelCount > 0U) {
      uint8_t lastChannel = config.builtInPeerDemo.channels[0U];
      for (uint8_t i = 0U; i < 4U; ++i) {
        if (i < config.builtInPeerDemo.channelCount) {
          lastChannel = config.builtInPeerDemo.channels[i];
        }
        packedDemoChannels |= (static_cast<uint32_t>(lastChannel) << (8U * i));
      }
    }
    sharedHost->reserved = packedDemoChannels;
  }

  BleCsControllerStreamHostConfig streamConfig{};
  streamConfig.session = config.session;
  streamConfig.controllerStream = &transport_;
  streamConfig.peerResultStream = config.peerResultStream;
  streamConfig.maxCommandsPerPump = config.maxCommandsPerPump;
  streamConfig.maxControllerBytesPerPoll = config.maxControllerBytesPerPoll;
  streamConfig.maxPeerBytesPerPoll = config.maxPeerBytesPerPoll;

  const bool ok = host_.begin(connHandle, streamConfig);
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::beginFreshHost(
    uint16_t connHandle,
    const BleCsControllerVprHostConfig& config,
    uint8_t maxPumpCount,
    uint8_t* outPumpCount) {
  if (outPumpCount != nullptr) {
    *outPumpCount = 0U;
  }

  bool ok = resetTransport(true);
  ok = ok && loadDefaultTransportImage();
  ok = ok && bootTransport();
  ok = ok && beginHost(connHandle, config);

  while (ok && !ready() && !failed()) {
    if (outPumpCount != nullptr && *outPumpCount >= maxPumpCount) {
      break;
    }
    ok = loopOnce();
    if (outPumpCount != nullptr) {
      *outPumpCount = static_cast<uint8_t>(*outPumpCount + 1U);
    }
  }
  return ok;
}

bool BleCsControllerVprHost::beginFreshHostFromBleConnection(
    VprControllerServiceHost& sourceService,
    const BleCsControllerVprHostConfig& config,
    uint8_t maxPumpCount,
    uint8_t* outPumpCount,
    VprBleConnectionSharedState* outImportedState,
    uint32_t sourceStateTimeoutMs) {
  if (outPumpCount != nullptr) {
    *outPumpCount = 0U;
  }
  if (outImportedState != nullptr) {
    *outImportedState = VprBleConnectionSharedState{};
  }

  VprBleConnectionSharedState importedState{};
  if (!sourceService.readBleConnectionSharedState(&importedState)) {
    return false;
  }
  if (!importedState.connected || importedState.connHandle == 0U) {
    if (!sourceService.waitBleConnectionSharedState(true, 1U, &importedState,
                                                    sourceStateTimeoutMs)) {
      return false;
    }
  }
  if (!importedState.connected || importedState.connHandle == 0U) {
    return false;
  }

  if (outImportedState != nullptr) {
    *outImportedState = importedState;
  }

  bool ok = resetTransport(true);
  ok = ok && loadDefaultTransportImage();
  ok = ok && writeBleConnectionBootHandoff(nrf54l15_vpr_transport_host_shared(),
                                           importedState);
  ok = ok && bootTransport();
  syncVprState();

  uint16_t connHandle = importedState.connHandle;
  if (vprState_.linkSessionOpen && vprState_.linkConnHandle != 0U) {
    connHandle = vprState_.linkConnHandle;
  }

  ok = ok && beginHost(connHandle, config);

  while (ok && !ready() && !failed()) {
    if (outPumpCount != nullptr && *outPumpCount >= maxPumpCount) {
      break;
    }
    ok = loopOnce();
    if (outPumpCount != nullptr) {
      *outPumpCount = static_cast<uint8_t>(*outPumpCount + 1U);
    }
  }
  return ok;
}

bool BleCsControllerVprHost::beginFreshWorkflowFromBleConnection(
    VprControllerServiceHost& sourceService,
    const BleCsControllerVprHostConfig& config,
    bool enableProcedure,
    uint8_t maxPumpCount,
    uint8_t* outPumpCount,
    VprBleConnectionSharedState* outImportedState,
    BleCsControllerVprWorkflowStartStatus* outWorkflowStatus,
    uint32_t sourceStateTimeoutMs) {
  const bool ok = beginFreshHostFromBleConnection(sourceService, config, maxPumpCount,
                                                  outPumpCount, outImportedState,
                                                  sourceStateTimeoutMs);
  return ok && directStartConfiguredWorkflow(enableProcedure, outWorkflowStatus);
}

bool BleCsControllerVprHost::directStartConfiguredWorkflow(
    bool enableProcedure,
    BleCsControllerVprWorkflowStartStatus* outWorkflowStatus) {
  if (outWorkflowStatus != nullptr) {
    *outWorkflowStatus = BleCsControllerVprWorkflowStartStatus{};
  }

  const BleCsControllerWorkflowConfig& workflowConfig = config_.session.workflow;
  uint8_t status = 0xFFU;

  if (!directReadRemoteSupportedCapabilities(&status)) {
    if (outWorkflowStatus != nullptr) {
      outWorkflowStatus->readRemoteSupportedCapabilities = status;
    }
    return false;
  }
  if (outWorkflowStatus != nullptr) {
    outWorkflowStatus->readRemoteSupportedCapabilities = status;
  }
  if (status != 0U) {
    return false;
  }

  if (workflowConfig.applyDefaultSettings) {
    status = 0xFFU;
    if (!directSetDefaultSettings(workflowConfig.defaultSettings, &status)) {
      if (outWorkflowStatus != nullptr) {
        outWorkflowStatus->setDefaultSettings = status;
      }
      return false;
    }
    if (outWorkflowStatus != nullptr) {
      outWorkflowStatus->setDefaultSettings = status;
    }
    if (status != 0U) {
      return false;
    }
  }

  status = 0xFFU;
  if (!directCreateConfig(workflowConfig.createConfig, &status)) {
    if (outWorkflowStatus != nullptr) {
      outWorkflowStatus->createConfig = status;
    }
    return false;
  }
  if (outWorkflowStatus != nullptr) {
    outWorkflowStatus->createConfig = status;
  }
  if (status != 0U) {
    return false;
  }

  if (workflowConfig.requireSecurityEnable) {
    status = 0xFFU;
    if (!directSecurityEnable(&status)) {
      if (outWorkflowStatus != nullptr) {
        outWorkflowStatus->securityEnable = status;
      }
      return false;
    }
    if (outWorkflowStatus != nullptr) {
      outWorkflowStatus->securityEnable = status;
    }
    if (status != 0U) {
      return false;
    }
  }

  status = 0xFFU;
  if (!directSetProcedureParameters(workflowConfig.procedureParameters, &status)) {
    if (outWorkflowStatus != nullptr) {
      outWorkflowStatus->setProcedureParameters = status;
    }
    return false;
  }
  if (outWorkflowStatus != nullptr) {
    outWorkflowStatus->setProcedureParameters = status;
  }
  if (status != 0U) {
    return false;
  }

  if (enableProcedure) {
    status = 0xFFU;
    bool started = directProcedureEnable(workflowConfig.procedureEnable, &status);
    if (!started || status != 0U) {
      for (uint8_t i = 0U; i < 8U && !failed(); ++i) {
        if (!poll()) {
          started = false;
          break;
        }
        if (vprState_.linkProcedureEnabled ||
            sessionState().completedProcedureCounter > 0U ||
            hostState().localSubeventResults > 0U ||
            hostState().peerSubeventResults > 0U) {
          status = 0U;
          started = true;
          break;
        }
      }
    }
    if (!started) {
      if (outWorkflowStatus != nullptr) {
        outWorkflowStatus->procedureEnable = status;
      }
      return false;
    }
    if (outWorkflowStatus != nullptr) {
      outWorkflowStatus->procedureEnable = status;
    }
    if (status != 0U) {
      return false;
    }
  }

  return true;
}

bool BleCsControllerVprHost::sendDirectHciCommand(uint16_t opcode,
                                                  const uint8_t* params,
                                                  size_t paramsLen,
                                                  uint8_t* response,
                                                  size_t responseSize,
                                                  size_t* responseLen) {
  bool resetRunStateBefore = false;
  bool resetRunStateAfter = false;
  const bool peerExchangeDebugCommand =
      opcode == kBleCsVprHciOpPeerPduInject ||
      opcode == kBleCsVprHciOpPeerStageRead ||
      opcode == kBleCsVprHciOpPendingLocalPduRead ||
      opcode == kBleCsVprHciOpSchedulerRead ||
      opcode == kBleCsVprHciOpSecurityMaterialRead ||
      opcode == kBleCsVprHciOpMeasurementWorkRead ||
      opcode == kBleCsVprHciOpMeasurementSnapshotRead ||
      opcode == kBleCsVprHciOpToneSnapshotRead;
  switch (opcode) {
    case kBleCsHciOpCreateConfig:
    case kBleCsHciOpSetProcedureParameters:
    case kBleCsHciOpProcedureEnable:
      resetRunStateBefore = true;
      break;
    case kBleCsHciOpRemoveConfig:
      resetRunStateAfter = true;
      break;
    default:
      break;
  }
  if (resetRunStateBefore) {
    host_.resetProcedureRunState();
  }

  /* Pre-drain any pending VPR events before sending the command.
   * The VPR may produce background events (e.g. demo-mode CS subevent
   * results) that set vprFlags=PENDING in shared memory, which causes
   * writeInternal to reject the write.  Draining those events into a
   * scratch host ensures the transport is clear for the new command.
   *
   * Retry up to 4 times: the VPR main loop can publish a new event
   * between our drain and the write, re-arming vprFlags=PENDING. */
  if (!peerExchangeDebugCommand) {
    for (uint8_t retry = 0U; retry < 4U; ++retry) {
      VprControllerServiceHost scratch(&transport_);
      (void)drainDirectControllerEvents(&scratch, nullptr, 0U, false, false);
      /* Force poll()->pullResponse() to clear vprFlags=PENDING in shared
       * memory, then consume anything that arrived so rxIndex catches up. */
      (void)transport_.available();
    }
  }

  VprControllerServiceHost directHost(&transport_);
  const bool ok =
      directHost.sendHciCommand(opcode, params, paramsLen, response, responseSize, responseLen);
  const bool drained =
      ok && (peerExchangeDebugCommand ||
             drainDirectControllerEvents(&directHost, response,
                                         (responseLen != nullptr) ? *responseLen : 0U,
                                         opcode != kBleCsHciOpProcedureEnable,
                                         opcode == kBleCsVprHciOpMeasurementExecute ||
                                             opcode == kBleCsVprHciOpMeasurementSnapshotRead));
  if (ok && drained && resetRunStateAfter) {
    host_.resetProcedureRunState();
  }
  syncVprState();
  if (opcode == kBleCsVprHciOpMeasurementExecute ||
      opcode == kBleCsVprHciOpMeasurementSnapshotRead) {
    return ok;
  }
  return ok && drained;
}

bool BleCsControllerVprHost::currentConnHandle(uint16_t* outConnHandle) const {
  if (outConnHandle == nullptr) {
    return false;
  }

  uint16_t connHandle = workflowState().connHandle;
  if (connHandle == 0U) {
    connHandle = vprState_.linkConnHandle;
  }
  if (connHandle == 0U) {
    return false;
  }

  *outConnHandle = connHandle;
  return true;
}

bool BleCsControllerVprHost::buildConnectedMode2ResultHeader(
    uint8_t fallbackConfigId,
    uint8_t numAntennaPaths,
    BleCsSubeventResultHeader* outHeader) const {
  if (outHeader == nullptr) {
    return false;
  }

  uint16_t connHandle = 0U;
  if (!currentConnHandle(&connHandle)) {
    return false;
  }

  BleCsSubeventResultHeader header{};
  header.connHandle = connHandle;
  header.configId = fallbackConfigId;
  header.procedureCounter =
      static_cast<uint16_t>(sessionState().completedProcedureCounter + 1U);
  if (header.procedureCounter == 0U) {
    header.procedureCounter = 1U;
  }

  const BleCsVprSchedulerState& scheduler = vprState_.scheduler;
  const bool schedulerMatchesLink =
      scheduler.valid && scheduler.status == 0U &&
      scheduler.procedureCounter != 0U && scheduler.connHandle == connHandle;
  if (schedulerMatchesLink) {
    if (scheduler.configId != 0U) {
      header.configId = scheduler.configId;
    }
    header.procedureCounter = scheduler.procedureCounter;
  }

  header.numAntennaPaths = (numAntennaPaths == 0U) ? 1U : numAntennaPaths;
  *outHeader = header;
  return true;
}

bool BleCsControllerVprHost::sendDirectBuiltCommand(const BleCsHciCommand& command,
                                                    uint8_t* outStatus) {
  uint8_t response[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA] = {0};
  size_t responseLen = 0U;
  if (!sendDirectHciCommand(command.opcode, command.payload, command.payloadLen, response,
                            sizeof(response), &responseLen)) {
    return false;
  }
  return parseDirectStatusResponse(response, responseLen, command.opcode, outStatus);
}

bool BleCsControllerVprHost::directInjectPeerPduForTest(
    const uint8_t* pdu, size_t pduLen, BleCsVprPeerExchangeState* outState) {
  if (pdu == nullptr || outState == nullptr || pduLen < 2U ||
      pduLen > kBleCsMaxHciCommandPayloadBytes) {
    return false;
  }

  uint8_t response[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA] = {0};
  size_t responseLen = 0U;
  if (!sendDirectHciCommand(kBleCsVprHciOpPeerPduInject, pdu, pduLen,
                            response, sizeof(response), &responseLen)) {
    return false;
  }
  return parseVprPeerExchangeResponse(response, responseLen,
                                      kBleCsVprHciOpPeerPduInject, outState);
}

bool BleCsControllerVprHost::directReadPeerExchangeStateForTest(
    BleCsVprPeerExchangeState* outState) {
  if (outState == nullptr) {
    return false;
  }

  uint8_t response[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA] = {0};
  size_t responseLen = 0U;
  if (!sendDirectHciCommand(kBleCsVprHciOpPeerStageRead, nullptr, 0U,
                            response, sizeof(response), &responseLen)) {
    return false;
  }
  return parseVprPeerExchangeResponse(response, responseLen,
                                      kBleCsVprHciOpPeerStageRead, outState);
}

bool BleCsControllerVprHost::directReadPendingLocalLlControlPduForTest(
    BleCsLlControlPdu* outPdu,
    BleCsVprPeerExchangeState* outState) {
  if (outPdu == nullptr || outState == nullptr) {
    return false;
  }

  uint8_t response[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA] = {0};
  size_t responseLen = 0U;
  if (!sendDirectHciCommand(kBleCsVprHciOpPendingLocalPduRead, nullptr, 0U,
                            response, sizeof(response), &responseLen)) {
    return false;
  }
  return parseVprPendingLocalLlControlPduResponse(
      response, responseLen, kBleCsVprHciOpPendingLocalPduRead, outPdu,
      outState);
}

bool BleCsControllerVprHost::directReadSchedulerStateForTest(
    BleCsVprSchedulerState* outState) {
  if (outState == nullptr) {
    return false;
  }

  uint8_t response[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA] = {0};
  size_t responseLen = 0U;
  if (!sendDirectHciCommand(kBleCsVprHciOpSchedulerRead, nullptr, 0U,
                            response, sizeof(response), &responseLen)) {
    return false;
  }
  if (!parseVprSchedulerStateResponse(response, responseLen,
                                      kBleCsVprHciOpSchedulerRead, outState)) {
    return false;
  }
  vprState_.scheduler = *outState;
  vprState_.linkProcedureCounter = outState->procedureCounter;
  return true;
}

bool BleCsControllerVprHost::directReadMeasurementWorkItemForTest(
    BleCsVprMeasurementWorkItem* outWork) {
  if (outWork == nullptr) {
    return false;
  }

  uint8_t response[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA] = {0};
  size_t responseLen = 0U;
  if (!sendDirectHciCommand(kBleCsVprHciOpMeasurementWorkRead, nullptr, 0U,
                            response, sizeof(response), &responseLen)) {
    return false;
  }
  if (!parseVprMeasurementWorkItemResponse(response, responseLen,
                                           kBleCsVprHciOpMeasurementWorkRead,
                                           outWork)) {
    return false;
  }
  vprState_.measurementWork = *outWork;
  if (outWork->procedureCounter != 0U) {
    vprState_.linkProcedureCounter = outWork->procedureCounter;
  }
  return true;
}

bool BleCsControllerVprHost::directReadSecurityMaterialForTest(
    BleCsVprSecurityMaterialState* outState) {
  if (outState == nullptr) {
    return false;
  }

  uint8_t response[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA] = {0};
  size_t responseLen = 0U;
  if (!sendDirectHciCommand(kBleCsVprHciOpSecurityMaterialRead, nullptr, 0U,
                            response, sizeof(response), &responseLen)) {
    return false;
  }
  return parseVprSecurityMaterialResponse(response, responseLen,
                                          kBleCsVprHciOpSecurityMaterialRead,
                                          outState);
}

bool BleCsControllerVprHost::readMeasurementExecutionSnapshot(
    BleCsVprMeasurementExecutionResult* outResult) {
  if (outResult == nullptr) {
    return false;
  }

  uint8_t response[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA] = {0};
  size_t responseLen = 0U;
  if (!sendDirectHciCommand(kBleCsVprHciOpMeasurementSnapshotRead, nullptr, 0U,
                            response, sizeof(response), &responseLen)) {
    return false;
  }
  if (!parseVprMeasurementExecutionResponse(
          response, responseLen, kBleCsVprHciOpMeasurementSnapshotRead,
          outResult)) {
    return false;
  }
  vprState_.measurementExecution = *outResult;
  if (outResult->procedureCounter != 0U) {
    vprState_.linkProcedureCounter = outResult->procedureCounter;
  }
  return true;
}

bool BleCsControllerVprHost::executeMeasurementWork(
    BleCsVprMeasurementExecutionResult* outResult,
    const BleCsMeasurementExecuteParams& params) {
  if (outResult == nullptr) {
    return false;
  }

  uint8_t commandParams[6] = {0};
  commandParams[0] = params.packetS0;
  commandParams[1] = params.packetCteInfo;
  writeLe16(&commandParams[2], params.controlToProbeDelayUs);
  writeLe16(&commandParams[4], params.responseListenWindowUs);

  uint8_t response[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA] = {0};
  size_t responseLen = 0U;
  if (!sendDirectHciCommand(kBleCsVprHciOpMeasurementExecute, commandParams,
                            sizeof(commandParams), response, sizeof(response),
                            &responseLen)) {
    return false;
  }
  if (!parseVprMeasurementExecutionResponse(
          response, responseLen, kBleCsVprHciOpMeasurementExecute, outResult)) {
    return false;
  }
  vprState_.measurementExecution = *outResult;
  if (outResult->procedureCounter != 0U) {
    vprState_.linkProcedureCounter = outResult->procedureCounter;
  }
  return true;
}

bool BleCsControllerVprHost::directExecuteMeasurementWorkForTest(
    BleCsVprMeasurementExecutionResult* outResult,
    const BleCsConfig* radioConfig) {
  BleCsMeasurementExecuteParams params{};
  if (radioConfig != nullptr) {
    params.packetS0 = radioConfig->s0Pattern;
    params.packetCteInfo = radioConfig->cteTimeUnits;
    params.controlToProbeDelayUs = radioConfig->controlToProbeDelayUs;
    params.responseListenWindowUs = radioConfig->responseListenWindowUs;
  }
  return executeMeasurementWork(outResult, params);
}

bool BleCsControllerVprHost::directReadToneSnapshotForTest(
    BleCsVprToneSnapshotResult* outResult) {
  if (outResult == nullptr) {
    return false;
  }

  uint8_t response[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA] = {0};
  size_t responseLen = 0U;
  if (!sendDirectHciCommand(kBleCsVprHciOpToneSnapshotRead, nullptr, 0U,
                            response, sizeof(response), &responseLen)) {
    return false;
  }
  return parseVprToneSnapshotResponse(
      response, responseLen, kBleCsVprHciOpToneSnapshotRead, outResult);
}

bool BleCsControllerVprHost::buildPendingInitiatorLlControlPdu(
    BleCsLlControlPdu* outPdu,
    BleCsVprPeerExchangeState* outState,
    bool* outVprOwned) {
  if (outPdu == nullptr) {
    return false;
  }
  if (outVprOwned != nullptr) {
    *outVprOwned = false;
  }

  BleCsVprPeerExchangeState vprOwnedState{};
  if (directReadPendingLocalLlControlPduForTest(outPdu, &vprOwnedState)) {
    if (outState != nullptr) {
      *outState = vprOwnedState;
    }
    if (outVprOwned != nullptr) {
      *outVprOwned = true;
    }
    return true;
  }

  BleCsVprPeerExchangeState state{};
  if (!directReadPeerExchangeStateForTest(&state) || !state.valid ||
      state.status != 0U) {
    if (outState != nullptr) {
      *outState = state;
    }
    return false;
  }

  if (outState != nullptr) {
    *outState = state;
  }

  const BleCsControllerWorkflowConfig& workflow = config_.session.workflow;
  uint8_t configId = workflow.createConfig.configId;
  if (configId == 0U) {
    configId = workflow.procedureParameters.configId;
  }
  if (configId == 0U) {
    configId = workflow.procedureEnable.configId;
  }
  if (configId == 0U) {
    configId = vprState_.linkConfigId;
  }
  if (configId == 0U) {
    configId = 1U;
  }

  switch (state.currentStage) {
    case kBleCsVprPeerStageAwaitingCsRsp:
      return bleCsBuildLlControlReq(configId, true, outPdu);

    case kBleCsVprPeerStageAwaitingSecRsp:
      return bleCsBuildLlControlSecurityReq(configId, outPdu);

    case kBleCsVprPeerStageAwaitingProcRsp: {
      BleCsLlControlProcedureParams params{};
      params.configId = workflow.procedureParameters.configId != 0U
                            ? workflow.procedureParameters.configId
                            : configId;
      params.maxProcedureLen = workflow.procedureParameters.maxProcedureLen;
      params.minProcedureInterval =
          workflow.procedureParameters.minProcedureInterval;
      params.maxProcedureInterval =
          workflow.procedureParameters.maxProcedureInterval;
      params.maxProcedureCount = workflow.procedureParameters.maxProcedureCount;
      params.minSubeventLen = workflow.procedureParameters.minSubeventLen;
      params.maxSubeventLen = workflow.procedureParameters.maxSubeventLen;
      params.phy = workflow.procedureParameters.phy;
      params.txPowerDelta = workflow.procedureParameters.txPowerDelta;
      return bleCsBuildLlControlProcedureReq(params, outPdu);
    }

    default:
      return false;
  }
}

bool BleCsControllerVprHost::queuePendingInitiatorLlControlPdu(
    BleRadio& radio,
    BleCsVprPeerExchangeState* outState,
    BleCsLlControlPdu* outPdu,
    bool* outVprOwned) {
  BleCsLlControlPdu pdu{};
  BleCsVprPeerExchangeState state{};
  bool vprOwned = false;
  if (!buildPendingInitiatorLlControlPdu(&pdu, &state, &vprOwned)) {
    if (outState != nullptr) {
      *outState = state;
    }
    if (outPdu != nullptr) {
      *outPdu = pdu;
    }
    if (outVprOwned != nullptr) {
      *outVprOwned = false;
    }
    return false;
  }

  if (outState != nullptr) {
    *outState = state;
  }
  if (outPdu != nullptr) {
    *outPdu = pdu;
  }
  if (outVprOwned != nullptr) {
    *outVprOwned = vprOwned;
  }
  return radio.queueChannelSoundingLlControlPdu(pdu.data(), pdu.length);
}

static bool bleCsVprStageNeedsInitiatorLlControlPdu(uint8_t stage) {
  return stage == kBleCsVprPeerStageAwaitingCsRsp ||
         stage == kBleCsVprPeerStageAwaitingSecRsp ||
         stage == kBleCsVprPeerStageAwaitingProcRsp;
}

void BleCsControllerVprHost::resetLlControlBridgeQueueState() {
  llControlBridgeQueuedStageValid_ = false;
  llControlBridgeQueuedStage_ = kBleCsVprPeerStageIdle;
  llControlBridgeNoTxStageValid_ = false;
  llControlBridgeNoTxStage_ = kBleCsVprPeerStageIdle;
  llControlBridgeNoTxPollSkips_ = 0U;
}

bool BleCsControllerVprHost::llControlBridgeCachedNoTxStage(
    BleCsLlControlBridgeServiceResult* result) {
  static constexpr uint8_t kNoTxStageReadIntervalPolls = 96U;
  if (!llControlBridgeNoTxStageValid_) {
    return false;
  }
  if (llControlBridgeNoTxPollSkips_ >= kNoTxStageReadIntervalPolls) {
    llControlBridgeNoTxPollSkips_ = 0U;
    return false;
  }

  ++llControlBridgeNoTxPollSkips_;
  if (result != nullptr) {
    result->state.valid = true;
    result->state.status = 0U;
    result->state.currentStage = llControlBridgeNoTxStage_;
    result->peerState = result->state;
  }
  return true;
}

bool BleCsControllerVprHost::queuePendingInitiatorLlControlPduIfNeeded(
    BleRadio& radio,
    BleCsLlControlBridgeServiceResult* result) {
  if (llControlBridgeCachedNoTxStage(result)) {
    return true;
  }

  BleCsVprPeerExchangeState state{};
  if (!directReadPeerExchangeStateForTest(&state)) {
    if (result != nullptr) {
      result->state = state;
    }
    return false;
  }

  if (result != nullptr) {
    result->state = state;
  }
  if (!state.valid || state.status != 0U) {
    return false;
  }
  if (!bleCsVprStageNeedsInitiatorLlControlPdu(state.currentStage)) {
    llControlBridgeQueuedStageValid_ = false;
    llControlBridgeQueuedStage_ = kBleCsVprPeerStageIdle;
    llControlBridgeNoTxStageValid_ =
        state.currentStage == kBleCsVprPeerStageAwaitingStart ||
        state.currentStage == kBleCsVprPeerStageProcedureActive;
    llControlBridgeNoTxStage_ = state.currentStage;
    llControlBridgeNoTxPollSkips_ = 0U;
    if (result != nullptr) {
      result->peerState = state;
    }
    return true;
  }

  if (llControlBridgeQueuedStageValid_ &&
      llControlBridgeQueuedStage_ == state.currentStage) {
    if (result != nullptr) {
      result->peerState = state;
    }
    return true;
  }

  BleCsLlControlPdu pdu{};
  bool vprOwned = false;
  if (!queuePendingInitiatorLlControlPdu(radio, &state, &pdu, &vprOwned)) {
    if (result != nullptr) {
      result->state = state;
    }
    return false;
  }

  llControlBridgeQueuedStageValid_ = true;
  llControlBridgeQueuedStage_ = state.currentStage;
  llControlBridgeNoTxStageValid_ = false;
  llControlBridgeNoTxStage_ = kBleCsVprPeerStageIdle;
  llControlBridgeNoTxPollSkips_ = 0U;

  if (result != nullptr) {
    result->initiatorPduQueued = true;
    result->initiatorPduSourceVpr = vprOwned;
    result->state = state;
    result->peerState = state;
    result->txOpcode = pdu.length > 0U ? pdu.bytes[0] : 0U;
  }
  return true;
}

bool BleCsControllerVprHost::consumePeerLlControlPdu(
    const uint8_t* payload,
    uint8_t length,
    BleCsVprPeerExchangeState* outState) {
  if (!bleCsLlControlPduIsValid(payload, length) || outState == nullptr) {
    return false;
  }

  return directInjectPeerPduForTest(payload, length, outState);
}

bool BleCsControllerVprHost::consumePeerLlControlPduFromEvent(
    const BleConnectionEvent& event,
    BleCsVprPeerExchangeState* outState) {
  if (!event.packetReceived || !event.crcOk || !event.packetIsNew ||
      !event.channelSoundingLlControlPacket || event.payload == nullptr) {
    return false;
  }

  return consumePeerLlControlPdu(event.payload, event.payloadLength, outState);
}

bool BleCsControllerVprHost::serviceInitiatorLlControlBridge(
    BleRadio& radio,
    const BleConnectionEvent* event,
    BleCsLlControlBridgeServiceResult* outResult) {
  BleCsLlControlBridgeServiceResult result{};

  if (event != nullptr && event->packetReceived && event->crcOk &&
      event->packetIsNew && event->channelSoundingLlControlPacket &&
      event->payload != nullptr && event->payloadLength >= 2U) {
    result.rxOpcode = event->llControlOpcode;
    if (!consumePeerLlControlPduFromEvent(*event, &result.state) ||
        !result.state.valid || result.state.status != 0U) {
      if (outResult != nullptr) {
        *outResult = result;
      }
      return false;
    }
    result.peerPduConsumed = true;
    result.peerState = result.state;
    resetLlControlBridgeQueueState();

    if (event->llControlOpcode == kBleCsLlCtrlCfg &&
        result.state.currentStage == kBleCsVprPeerStageIdle) {
      uint8_t status = 0xFFU;
      result.directCommandSent = true;
      if (!directSecurityEnable(&status) || status != 0U) {
        result.directStatus = status;
        if (outResult != nullptr) {
          *outResult = result;
        }
        return false;
      }
      result.directStatus = status;
      const bool queued =
          queuePendingInitiatorLlControlPduIfNeeded(radio, &result);
      if (outResult != nullptr) {
        *outResult = result;
      }
      return queued;
    }

    if (event->llControlOpcode == kBleCsLlCtrlSecRsp &&
        result.state.currentStage == kBleCsVprPeerStageIdle) {
      uint8_t status = 0xFFU;
      result.directCommandSent = true;
      if (!directSetProcedureParameters(config_.session.workflow.procedureParameters,
                                        &status) ||
          status != 0U) {
        result.directStatus = status;
        if (outResult != nullptr) {
          *outResult = result;
        }
        return false;
      }
      result.directStatus = status;
      const bool queued =
          queuePendingInitiatorLlControlPduIfNeeded(radio, &result);
      if (outResult != nullptr) {
        *outResult = result;
      }
      return queued;
    }

    if (event->llControlOpcode == kBleCsLlCtrlProcRsp &&
        result.state.currentStage == kBleCsVprPeerStageAwaitingStart) {
      uint8_t status = 0xFFU;
      result.directCommandSent = true;
      if (!directProcedureEnable(config_.session.workflow.procedureEnable,
                                 &status) ||
          status != 0U) {
        result.directStatus = status;
        if (outResult != nullptr) {
          *outResult = result;
        }
        return false;
      }
      result.directStatus = status;
    }

    if (event->llControlOpcode == kBleCsLlCtrlStart &&
        result.state.currentStage == kBleCsVprPeerStageProcedureActive) {
      (void)drainPendingControllerEvents();
    }

    if (event->llControlOpcode == kBleCsLlCtrlAbort &&
        result.state.currentStage == kBleCsVprPeerStageIdle) {
      result.exchangeComplete = true;
    }

    if (outResult != nullptr) {
      *outResult = result;
    }
    return true;
  }

  const bool queued = queuePendingInitiatorLlControlPduIfNeeded(radio, &result);
  if (outResult != nullptr) {
    *outResult = result;
  }
  return queued;
}

bool BleCsControllerVprHost::pollInitiatorLlControlBridge(
    BleRadio& radio,
    BleCsLlControlBridgePollResult* outResult,
    uint32_t spinLimit) {
  BleCsLlControlBridgePollResult result{};

  BleCsLlControlBridgeServiceResult preService{};
  if (!serviceInitiatorLlControlBridge(radio, nullptr, &preService)) {
    result.service = preService;
    if (outResult != nullptr) {
      *outResult = result;
    }
    return false;
  }
  if (preService.peerPduConsumed || preService.initiatorPduQueued ||
      preService.directCommandSent || preService.exchangeComplete) {
    result.serviceCalled = true;
    result.preServiceCalled = true;
    result.preService = preService;
    result.service = preService;
  }

  BleConnectionEvent event{};
  const bool ran = radio.pollConnectionEvent(&event, spinLimit);
  result.pollRan = ran;
  if (!ran) {
    if (outResult != nullptr) {
      *outResult = result;
    }
    return true;
  }

  result.eventStarted = event.eventStarted;
  result.eventCounter = event.eventCounter;
  if (event.eventStarted && event.packetReceived && event.crcOk &&
      event.packetIsNew && event.channelSoundingLlControlPacket &&
      event.payload != nullptr && event.payloadLength >= 2U) {
    result.csLlControlReceived = true;
    result.rxOpcode = event.llControlOpcode;
    result.rxPayloadLength = event.payloadLength;

    BleCsLlControlBridgeServiceResult service{};
    if (!serviceInitiatorLlControlBridge(radio, &event, &service)) {
      result.serviceCalled = true;
      result.eventServiceCalled = true;
      result.eventService = service;
      result.service = service;
      if (outResult != nullptr) {
        *outResult = result;
      }
      return false;
    }
    result.serviceCalled = true;
    result.eventServiceCalled = true;
    result.eventService = service;
    result.service = service;
  }

  if (outResult != nullptr) {
    *outResult = result;
  }
  return true;
}

static void bleCsBridgeTrackerMarkBit(uint32_t* mask, uint8_t bit) {
  if (mask != nullptr && bit < 32U) {
    *mask |= (1UL << bit);
  }
}

static void bleCsBridgeTrackerMarkTx(BleCsLlControlBridgeWorkflowTracker* tracker,
                                     uint8_t opcode) {
  if (tracker == nullptr) {
    return;
  }
  switch (opcode) {
    case kBleCsLlCtrlReq:
      bleCsBridgeTrackerMarkBit(
          &tracker->txMask,
          BleCsLlControlBridgeWorkflowTracker::kTxCsReq);
      break;
    case kBleCsLlCtrlSecReq:
      bleCsBridgeTrackerMarkBit(
          &tracker->txMask,
          BleCsLlControlBridgeWorkflowTracker::kTxSecReq);
      break;
    case kBleCsLlCtrlProcReq:
      bleCsBridgeTrackerMarkBit(
          &tracker->txMask,
          BleCsLlControlBridgeWorkflowTracker::kTxProcReq);
      break;
    default:
      break;
  }
}

static void bleCsBridgeTrackerMarkRx(BleCsLlControlBridgeWorkflowTracker* tracker,
                                     uint8_t opcode) {
  if (tracker == nullptr) {
    return;
  }
  switch (opcode) {
    case kBleCsLlCtrlRsp:
      bleCsBridgeTrackerMarkBit(
          &tracker->rxMask,
          BleCsLlControlBridgeWorkflowTracker::kRxCsRsp);
      break;
    case kBleCsLlCtrlCfg:
      bleCsBridgeTrackerMarkBit(
          &tracker->rxMask,
          BleCsLlControlBridgeWorkflowTracker::kRxCsCfg);
      break;
    case kBleCsLlCtrlSecRsp:
      bleCsBridgeTrackerMarkBit(
          &tracker->rxMask,
          BleCsLlControlBridgeWorkflowTracker::kRxSecRsp);
      break;
    case kBleCsLlCtrlProcRsp:
      bleCsBridgeTrackerMarkBit(
          &tracker->rxMask,
          BleCsLlControlBridgeWorkflowTracker::kRxProcRsp);
      break;
    case kBleCsLlCtrlStart:
      bleCsBridgeTrackerMarkBit(
          &tracker->rxMask,
          BleCsLlControlBridgeWorkflowTracker::kRxStart);
      break;
    case kBleCsLlCtrlAbort:
      bleCsBridgeTrackerMarkBit(
          &tracker->rxMask,
          BleCsLlControlBridgeWorkflowTracker::kRxAbort);
      break;
    default:
      break;
  }
}

static void bleCsBridgeTrackerUpdateWorkflow(
    BleCsLlControlBridgeWorkflowTracker* tracker,
    const BleCsControllerVprHost& host) {
  if (tracker == nullptr) {
    return;
  }
  const BleCsControllerWorkflowState& workflow = host.workflowState();
  if (workflow.remoteCapabilitiesValid) {
    bleCsBridgeTrackerMarkBit(
        &tracker->workflowMask,
        BleCsLlControlBridgeWorkflowTracker::kWorkflowRemoteCapabilities);
  }
  if (workflow.defaultSettingsApplied) {
    bleCsBridgeTrackerMarkBit(
        &tracker->workflowMask,
        BleCsLlControlBridgeWorkflowTracker::kWorkflowDefaults);
  }
  if (workflow.configCreated) {
    bleCsBridgeTrackerMarkBit(
        &tracker->workflowMask,
        BleCsLlControlBridgeWorkflowTracker::kWorkflowConfig);
  }
  if (workflow.securityEnabled) {
    bleCsBridgeTrackerMarkBit(
        &tracker->workflowMask,
        BleCsLlControlBridgeWorkflowTracker::kWorkflowSecurity);
  }
  if (workflow.procedureParametersApplied) {
    bleCsBridgeTrackerMarkBit(
        &tracker->workflowMask,
        BleCsLlControlBridgeWorkflowTracker::kWorkflowProcedureParams);
  }
  if (workflow.procedureEnabled) {
    bleCsBridgeTrackerMarkBit(
        &tracker->workflowMask,
        BleCsLlControlBridgeWorkflowTracker::kWorkflowProcedureEnabled);
  }
  tracker->ready = host.ready();
  if (tracker->ready) {
    bleCsBridgeTrackerMarkBit(
        &tracker->workflowMask,
        BleCsLlControlBridgeWorkflowTracker::kWorkflowReady);
  }

  tracker->localSubeventResults = host.hostState().localSubeventResults;
  tracker->peerSubeventResults = host.hostState().peerSubeventResults;
  tracker->completedProcedureCounter =
      host.sessionState().completedProcedureCounter;
  tracker->estimateValid = host.estimateValid();
  tracker->resultPathComplete =
      tracker->localSubeventResults > 0U &&
      tracker->peerSubeventResults > 0U &&
      tracker->completedProcedureCounter > 0U &&
      tracker->estimateValid;
}

static void bleCsBridgeTrackerUpdateService(
    BleCsLlControlBridgeWorkflowTracker* tracker,
    const BleCsLlControlBridgeServiceResult& service) {
  if (tracker == nullptr) {
    return;
  }

  if (service.state.valid) {
    tracker->lastVprStage = service.state.currentStage;
    tracker->lastVprStatus = service.state.status;
  }

  if (service.peerPduConsumed) {
    ++tracker->peerPdusConsumed;
    tracker->lastRxOpcode = service.rxOpcode;
    bleCsBridgeTrackerMarkRx(tracker, service.rxOpcode);
  }
  if (service.initiatorPduQueued) {
    ++tracker->txQueued;
    if (service.initiatorPduSourceVpr) {
      ++tracker->vprPduQueued;
    }
    tracker->lastTxOpcode = service.txOpcode;
    bleCsBridgeTrackerMarkTx(tracker, service.txOpcode);
  }
  if (service.directCommandSent) {
    ++tracker->directCommands;
    tracker->lastDirectStatus = service.directStatus;
  }
  if (service.exchangeComplete) {
    tracker->exchangeComplete = true;
  }
}

void BleCsLlControlBridgeWorkflowTracker::reset() {
  *this = BleCsLlControlBridgeWorkflowTracker{};
}

void BleCsLlControlBridgeWorkflowTracker::update(
    const BleCsControllerVprHost& host,
    const BleCsLlControlBridgePollResult& poll) {
  if (polls < 0xFFFFU) {
    ++polls;
  }
  if (poll.eventStarted) {
    ++linkEvents;
  }

  bleCsBridgeTrackerUpdateWorkflow(this, host);
  if (poll.preServiceCalled) {
    bleCsBridgeTrackerUpdateService(this, poll.preService);
  }
  if (poll.eventServiceCalled) {
    bleCsBridgeTrackerUpdateService(this, poll.eventService);
  } else if (poll.serviceCalled && !poll.preServiceCalled) {
    bleCsBridgeTrackerUpdateService(this, poll.service);
  }
  bleCsBridgeTrackerUpdateWorkflow(this, host);
}

bool BleCsLlControlBridgeWorkflowTracker::workflowComplete() const {
  static constexpr uint32_t kExpected =
      (1UL << kWorkflowRemoteCapabilities) |
      (1UL << kWorkflowDefaults) |
      (1UL << kWorkflowConfig) |
      (1UL << kWorkflowSecurity) |
      (1UL << kWorkflowProcedureParams) |
      (1UL << kWorkflowProcedureEnabled) |
      (1UL << kWorkflowReady);
  return (workflowMask & kExpected) == kExpected;
}

bool BleCsLlControlBridgeWorkflowTracker::txComplete() const {
  static constexpr uint32_t kExpected =
      (1UL << kTxCsReq) |
      (1UL << kTxSecReq) |
      (1UL << kTxProcReq);
  return (txMask & kExpected) == kExpected;
}

bool BleCsLlControlBridgeWorkflowTracker::rxComplete() const {
  static constexpr uint32_t kExpected =
      (1UL << kRxCsRsp) |
      (1UL << kRxCsCfg) |
      (1UL << kRxSecRsp) |
      (1UL << kRxProcRsp) |
      (1UL << kRxStart) |
      (1UL << kRxAbort);
  return (rxMask & kExpected) == kExpected;
}

bool BleCsLlControlBridgeWorkflowTracker::complete() const {
  return workflowComplete() && txComplete() && rxComplete() &&
         resultPathComplete;
}

bool BleCsControllerVprHost::directReadRemoteSupportedCapabilities(uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciReadRemoteSupportedCapabilitiesCommand(connHandle,
                                                                                 &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directWriteCachedRemoteSupportedCapabilities(
    const BleCsControllerCapabilities& capabilities,
    uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  const bool ok = currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::
             buildHciWriteCachedRemoteSupportedCapabilitiesCommand(
                 connHandle, capabilities, &command) &&
         sendDirectBuiltCommand(command, outStatus);
  if (ok && (outStatus == nullptr || *outStatus == 0U)) {
    cachedRemoteCapabilitiesV1_ = capabilities;
    cachedRemoteCapabilitiesV1Valid_ = capabilities.valid;
  }
  return ok;
}

bool BleCsControllerVprHost::directWriteCachedRemoteSupportedCapabilitiesV2(
    const BleCsControllerCapabilities& capabilities,
    uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  const bool ok = currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::
             buildHciWriteCachedRemoteSupportedCapabilitiesV2Command(
                 connHandle, capabilities, &command) &&
         sendDirectBuiltCommand(command, outStatus);
  if (ok && (outStatus == nullptr || *outStatus == 0U)) {
    cachedRemoteCapabilitiesV2_ = capabilities;
    cachedRemoteCapabilitiesV2Valid_ = capabilities.valid;
  }
  return ok;
}

bool BleCsControllerVprHost::directReadRemoteFaeTable(BleCsFaeTable* outTable,
                                                      uint8_t* outStatus) {
  if (outTable == nullptr || outStatus == nullptr) {
    return false;
  }
  *outTable = BleCsFaeTable{};
  lastRemoteFaeTable_ = BleCsFaeTable{};

  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  if (!currentConnHandle(&connHandle) ||
      !BleChannelSoundingRadio::buildHciReadRemoteFaeTableCommand(
          connHandle, &command) ||
      !sendDirectBuiltCommand(command, outStatus)) {
    return false;
  }
  if (*outStatus != 0U) {
    return true;
  }
  if (!lastRemoteFaeTable_.valid ||
      lastRemoteFaeTable_.connHandle != connHandle) {
    return false;
  }
  *outTable = lastRemoteFaeTable_;
  return true;
}

bool BleCsControllerVprHost::directWriteCachedRemoteFaeTable(
    const int8_t faeTable[kBleCsFaeTableValueCount],
    uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  const bool ok = currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciWriteCachedRemoteFaeTableCommand(
             connHandle, faeTable, &command) &&
         sendDirectBuiltCommand(command, outStatus);
  if (ok && (outStatus == nullptr || *outStatus == 0U)) {
    memcpy(lastRemoteFaeTable_.values, faeTable, sizeof(lastRemoteFaeTable_.values));
    lastRemoteFaeTable_.valid = true;
    lastRemoteFaeTable_.status = 0U;
    lastRemoteFaeTable_.connHandle = connHandle;
  }
  return ok;
}

bool BleCsControllerVprHost::directSetChannelClassification(
    const BleCsChannelClassification& classification,
    uint8_t* outStatus) {
  BleCsHciCommand command{};
  return BleChannelSoundingRadio::buildHciSetChannelClassificationCommand(
             classification, &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directStartTest(const BleCsTestParams& params,
                                             uint8_t* outStatus) {
  lastTestEndComplete_ = BleCsTestEndComplete{};
  lastTestEndCompleteValid_ = false;
  testReassembler_.reset();
  lastTestResult_ = BleCsSubeventResult{};
  lastTestResultValid_ = false;
  testResultCount_ = 0U;
  BleCsHciCommand command{};
  return BleChannelSoundingRadio::buildHciTestCommand(params, &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directStopTest(
    BleCsTestEndComplete* outComplete,
    uint8_t* outStatus) {
  if (outComplete == nullptr || outStatus == nullptr) {
    return false;
  }
  *outComplete = BleCsTestEndComplete{};
  lastTestEndComplete_ = BleCsTestEndComplete{};
  lastTestEndCompleteValid_ = false;

  BleCsHciCommand command{};
  if (!BleChannelSoundingRadio::buildHciTestEndCommand(&command) ||
      !sendDirectBuiltCommand(command, outStatus)) {
    return false;
  }
  if (*outStatus != 0U) {
    return true;
  }
  if (!lastTestEndCompleteValid_) {
    return false;
  }
  *outComplete = lastTestEndComplete_;
  return true;
}

bool BleCsControllerVprHost::directSetDefaultSettings(const BleCsDefaultSettings& settings,
                                                      uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciSetDefaultSettingsCommand(connHandle, settings,
                                                                    &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directCreateConfig(const BleCsControllerCreateConfig& config,
                                                uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciCreateConfigCommand(connHandle, config, &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directRemoveConfig(uint8_t configId, uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciRemoveConfigCommand(connHandle, configId, &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directSecurityEnable(uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciSecurityEnableCommand(connHandle, &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directSetProcedureParameters(const BleCsProcedureParameters& params,
                                                          uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciSetProcedureParametersCommand(connHandle, params,
                                                                        &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directProcedureEnable(const BleCsProcedureEnable& params,
                                                   uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciProcedureEnableCommand(connHandle, params, &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directProcedureEnable(uint8_t configId,
                                                   bool enable,
                                                   uint8_t* outStatus) {
  BleCsProcedureEnable params{};
  params.configId = configId;
  params.enable = enable ? 1U : 0U;
  return directProcedureEnable(params, outStatus);
}

bool BleCsControllerVprHost::directCurrentProcedureEnable(bool enable,
                                                          uint8_t* outStatus) {
  return directProcedureEnable(workflowState().configComplete.configId, enable,
                               outStatus);
}

bool BleCsControllerVprHost::pollUntilRunningWithProcedureCount(
    uint16_t targetProcedureCount,
    uint8_t maxPolls,
    uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    const bool completed =
        sessionState().completedProcedureCounter >= targetProcedureCount;
    const bool running = vprState_.linkProcedureEnabled;
    if (completed && running) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilStopped(uint8_t maxPolls,
                                              uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    if (!vprState_.linkProcedureEnabled) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return !vprState_.linkProcedureEnabled;
}

bool BleCsControllerVprHost::pollUntilStoppedWithProcedureCount(uint16_t targetProcedureCount,
                                                                uint8_t maxPolls,
                                                                uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    const bool completed = sessionState().completedProcedureCounter >= targetProcedureCount;
    const bool stopped = !vprState_.linkProcedureEnabled;
    if (completed && stopped) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilStoppedOnConfig(uint8_t targetConfigId,
                                                      uint8_t maxPolls,
                                                      uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    const BleCsSubeventResult currentLocal = completedLocalResult();
    const BleCsSubeventResult currentPeer = completedPeerResult();
    const bool stopped = !vprState_.linkProcedureEnabled;
    if (stopped && currentLocal.header.configId == targetConfigId &&
        currentPeer.header.configId == targetConfigId) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilRunComplete(uint32_t targetLocalSubevents,
                                                  uint32_t targetPeerSubevents,
                                                  uint8_t maxPolls,
                                                  uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    const bool completed =
        hostState().localSubeventResults >= targetLocalSubevents &&
        hostState().peerSubeventResults >= targetPeerSubevents;
    const bool stopped = !vprState_.linkProcedureEnabled;
    if (completed && stopped) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilCompletedProcedureResult(
    uint16_t targetProcedureCount,
    uint32_t targetLocalSubevents,
    uint32_t targetPeerSubevents,
    uint8_t maxPolls,
    uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    const bool completed =
        sessionState().completedProcedureCounter >= targetProcedureCount &&
        hostState().localSubeventResults >= targetLocalSubevents &&
        hostState().peerSubeventResults >= targetPeerSubevents;
    if (completed) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilSelectedState(uint8_t selectedConfigId,
                                                    uint8_t storedCount,
                                                    bool selectedRunnable,
                                                    uint8_t maxPolls,
                                                    uint8_t* outPolls) {
  BleCsControllerVprSelectedStateExpectation expected{};
  expected.selectedConfigId = selectedConfigId;
  expected.storedConfigCount = storedCount;
  expected.selectedRunnable = selectedRunnable;
  return pollUntilSelectedState(expected, maxPolls, outPolls);
}

bool BleCsControllerVprHost::pollUntilSelectedState(
    const BleCsControllerVprSelectedStateExpectation& expected,
    uint8_t maxPolls,
    uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    if (vprState_.selectedStateMatches(expected)) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilRetainedSelectionState(uint8_t activeConfigId,
                                                             uint8_t slot0ConfigId,
                                                             uint8_t slot1ConfigId,
                                                             uint8_t previousConfigId,
                                                             uint8_t storedConfigCount,
                                                             bool selectedRunnable,
                                                             bool previousRunnable,
                                                             uint8_t maxPolls,
                                                             uint8_t* outPolls) {
  BleCsControllerVprRetainedSelectionExpectation expected{};
  expected.activeConfigId = activeConfigId;
  expected.slot0ConfigId = slot0ConfigId;
  expected.slot1ConfigId = slot1ConfigId;
  expected.previousConfigId = previousConfigId;
  expected.storedConfigCount = storedConfigCount;
  expected.selectedRunnable = selectedRunnable;
  expected.previousRunnable = previousRunnable;
  return pollUntilRetainedSelectionState(expected, maxPolls, outPolls);
}

bool BleCsControllerVprHost::pollUntilRetainedSelectionState(
    const BleCsControllerVprRetainedSelectionExpectation& expected,
    uint8_t maxPolls,
    uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    if (vprState_.retainedConfigMatches(expected)) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::settleDirectIdle(uint8_t stablePollsRequired,
                                              uint8_t maxPolls,
                                              uint8_t* outPolls) {
  uint8_t stablePolls = 0U;
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
    if (!vprState_.linkProcedureEnabled && transport_.available() == 0) {
      ++stablePolls;
      if (stablePolls >= stablePollsRequired) {
        return true;
      }
    } else {
      stablePolls = 0U;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
  }
  return !vprState_.linkProcedureEnabled && transport_.available() == 0;
}

bool BleCsControllerVprHost::pollUntilRetainedSlots(uint8_t activeConfigId,
                                                    uint8_t slot0ConfigId,
                                                    uint8_t slot1ConfigId,
                                                    uint8_t previousConfigId,
                                                    uint8_t activePrimarySlotIndex,
                                                    uint8_t freePrimarySlotCount,
                                                    uint8_t storedConfigCount,
                                                    uint8_t maxPolls,
                                                    uint8_t* outPolls) {
  BleCsControllerVprRetainedSlotsExpectation expected{};
  expected.activeConfigId = activeConfigId;
  expected.slot0ConfigId = slot0ConfigId;
  expected.slot1ConfigId = slot1ConfigId;
  expected.previousConfigId = previousConfigId;
  expected.activePrimarySlotIndex = activePrimarySlotIndex;
  expected.freePrimarySlotCount = freePrimarySlotCount;
  expected.storedConfigCount = storedConfigCount;
  return pollUntilRetainedSlots(expected, maxPolls, outPolls);
}

bool BleCsControllerVprHost::pollUntilRetainedSlots(
    const BleCsControllerVprRetainedSlotsExpectation& expected,
    uint8_t maxPolls,
    uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    if (vprState_.retainedConfigMatches(expected)) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilRetainedState(uint8_t activeConfigId,
                                                    uint8_t slot0ConfigId,
                                                    uint8_t slot1ConfigId,
                                                    uint8_t previousConfigId,
                                                    uint8_t activePrimarySlotIndex,
                                                    uint8_t freePrimarySlotCount,
                                                    uint8_t storedConfigCount,
                                                    bool selectedRunnable,
                                                    bool slot0Runnable,
                                                    bool slot1Runnable,
                                                    bool previousRunnable,
                                                    bool selectedSecurityEnabled,
                                                    bool slot0SecurityEnabled,
                                                    bool slot1SecurityEnabled,
                                                    bool previousSecurityEnabled,
                                                    bool selectedProcedureParamsApplied,
                                                    bool slot0ProcedureParamsApplied,
                                                    bool slot1ProcedureParamsApplied,
                                                    bool previousProcedureParamsApplied,
                                                    uint8_t maxPolls,
                                                    uint8_t* outPolls) {
  BleCsControllerVprRetainedStateExpectation expected{};
  expected.slots.activeConfigId = activeConfigId;
  expected.slots.slot0ConfigId = slot0ConfigId;
  expected.slots.slot1ConfigId = slot1ConfigId;
  expected.slots.previousConfigId = previousConfigId;
  expected.slots.activePrimarySlotIndex = activePrimarySlotIndex;
  expected.slots.freePrimarySlotCount = freePrimarySlotCount;
  expected.slots.storedConfigCount = storedConfigCount;
  expected.runnability.selectedRunnable = selectedRunnable;
  expected.runnability.slot0Runnable = slot0Runnable;
  expected.runnability.slot1Runnable = slot1Runnable;
  expected.runnability.previousRunnable = previousRunnable;
  expected.readiness.selectedSecurityEnabled = selectedSecurityEnabled;
  expected.readiness.slot0SecurityEnabled = slot0SecurityEnabled;
  expected.readiness.slot1SecurityEnabled = slot1SecurityEnabled;
  expected.readiness.previousSecurityEnabled = previousSecurityEnabled;
  expected.readiness.selectedProcedureParamsApplied =
      selectedProcedureParamsApplied;
  expected.readiness.slot0ProcedureParamsApplied = slot0ProcedureParamsApplied;
  expected.readiness.slot1ProcedureParamsApplied = slot1ProcedureParamsApplied;
  expected.readiness.previousProcedureParamsApplied =
      previousProcedureParamsApplied;
  expected.checkRunnability = true;
  expected.checkReadiness = true;
  return pollUntilRetainedState(expected, maxPolls, outPolls);
}

bool BleCsControllerVprHost::pollUntilRetainedState(
    const BleCsControllerVprRetainedStateExpectation& expected,
    uint8_t maxPolls,
    uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    if (vprState_.retainedConfigMatches(expected)) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pumpCommands() {
  const bool ok = host_.pumpCommands();
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::poll() {
  const bool ok = host_.poll();
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::loopOnce() {
  const bool ok = host_.loopOnce();
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::pollWithInitiatorLlControlBridge(
    BleRadio& radio,
    BleCsLlControlBridgePollResult* outResult,
    uint32_t spinLimit) {
  if (!poll()) {
    if (outResult != nullptr) {
      *outResult = BleCsLlControlBridgePollResult{};
    }
    return false;
  }
  return pollInitiatorLlControlBridge(radio, outResult, spinLimit);
}

bool BleCsControllerVprHost::initiatorLlBridgeOwnsCurrentWorkflowPhase() const {
  switch (workflowState().phase) {
    case BleCsControllerWorkflowPhase::kNeedSecurityEnable:
    case BleCsControllerWorkflowPhase::kWaitingSecurityEnableComplete:
    case BleCsControllerWorkflowPhase::kNeedSetProcedureParameters:
    case BleCsControllerWorkflowPhase::kWaitingSetProcedureParameters:
    case BleCsControllerWorkflowPhase::kNeedProcedureEnable:
    case BleCsControllerWorkflowPhase::kWaitingProcedureEnableComplete:
      return true;
    default:
      return false;
  }
}

bool BleCsControllerVprHost::loopOnceWithInitiatorLlControlBridge(
    BleRadio& radio,
    BleCsLlControlBridgePollResult* outResult,
    uint32_t spinLimit) {
  const bool bridgeOwnedBeforePump = initiatorLlBridgeOwnsCurrentWorkflowPhase();
  const bool ok = bridgeOwnedBeforePump ? poll() : loopOnce();
  if (!ok) {
    if (outResult != nullptr) {
      *outResult = BleCsLlControlBridgePollResult{};
    }
    return false;
  }
  if (!initiatorLlBridgeOwnsCurrentWorkflowPhase() && !ready()) {
    if (outResult != nullptr) {
      *outResult = BleCsLlControlBridgePollResult{};
    }
    return true;
  }
  return pollInitiatorLlControlBridge(radio, outResult, spinLimit);
}

bool BleCsControllerVprHost::pumpInitiatorLlControlWorkflowBridge(
    BleRadio& radio,
    BleCsLlControlBridgeWorkflowTracker* tracker,
    BleCsLlControlBridgePollResult* outLastPoll,
    uint16_t maxPolls,
    uint32_t spinLimit) {
  if (maxPolls == 0U) {
    maxPolls = 1U;
  }

  for (uint16_t i = 0U; i < maxPolls; ++i) {
    BleCsLlControlBridgePollResult poll{};
    if (!loopOnceWithInitiatorLlControlBridge(radio, &poll, spinLimit)) {
      if (outLastPoll != nullptr) {
        *outLastPoll = poll;
      }
      return false;
    }
    if (tracker != nullptr) {
      tracker->update(*this, poll);
    }
    if (outLastPoll != nullptr) {
      *outLastPoll = poll;
    }
    if (tracker != nullptr && tracker->complete()) {
      break;
    }
  }
  return true;
}

bool BleCsControllerVprHost::drainPendingControllerEvents() {
  if (!host_.hostState().began) {
    return false;
  }
  syncVprState();
  const bool waitForConnectedCsResult =
      vprState_.linkMeasurementExecuteResultActive ||
      vprState_.linkResultPendingStage != 0U;
  VprControllerServiceHost directHost(&transport_);
  return drainDirectControllerEvents(&directHost, nullptr, 0U,
                                    waitForConnectedCsResult,
                                    waitForConnectedCsResult);
}

bool BleCsControllerVprHost::drainPendingConnectedControllerEvents() {
  if (!host_.hostState().began) {
    return false;
  }

  VprControllerServiceHost directHost(&transport_);
  return drainDirectControllerEvents(&directHost, nullptr, 0U, true, true);
}

bool BleCsControllerVprHost::consumeCompletedResult(
    BleCsControllerResultSource source,
    const BleCsSubeventResult& result) {
  return host_.consumeCompletedResult(source, result);
}

bool BleCsControllerVprHost::consumeMode2ResultsFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    const BleCsSubeventResultHeader& headerTemplate,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen) {
  return host_.consumeMode2ResultsFromMeasurements(
      measurements, count, headerTemplate, localStepData, localMaxStepDataLen,
      peerStepData, peerMaxStepDataLen);
}

bool BleCsControllerVprHost::consumeMode2ResultEventsFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    const BleCsSubeventResultHeader& headerTemplate,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen) {
  return host_.consumeMode2ResultEventsFromMeasurements(
      measurements, count, headerTemplate, localStepData, localMaxStepDataLen,
      peerStepData, peerMaxStepDataLen);
}

bool BleCsControllerVprHost::consumeMode2ControllerEventsFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    const BleCsSubeventResultHeader& headerTemplate,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen) {
  return host_.consumeMode2ControllerEventsFromMeasurements(
      measurements, count, headerTemplate, localStepData, localMaxStepDataLen,
      peerStepData, peerMaxStepDataLen);
}

bool BleCsControllerVprHost::consumeConnectedMode2ResultsFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    uint8_t configId,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen,
    uint8_t numAntennaPaths) {
  BleCsSubeventResultHeader header{};
  if (!buildConnectedMode2ResultHeader(configId, numAntennaPaths, &header)) {
    return false;
  }

  return consumeMode2ResultsFromMeasurements(
      measurements, count, header, localStepData, localMaxStepDataLen,
      peerStepData, peerMaxStepDataLen);
}

bool BleCsControllerVprHost::consumeConnectedMode2ResultEventsFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    uint8_t configId,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen,
    uint8_t numAntennaPaths) {
  BleCsSubeventResultHeader header{};
  if (!buildConnectedMode2ResultHeader(configId, numAntennaPaths, &header)) {
    return false;
  }

  return consumeMode2ResultEventsFromMeasurements(
      measurements, count, header, localStepData, localMaxStepDataLen,
      peerStepData, peerMaxStepDataLen);
}

bool BleCsControllerVprHost::consumeConnectedMode2ControllerEventsFromMeasurements(
    const BleCsChannelMeasurement* measurements,
    size_t count,
    uint8_t configId,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen,
    uint8_t numAntennaPaths) {
  BleCsSubeventResultHeader header{};
  if (!buildConnectedMode2ResultHeader(configId, numAntennaPaths, &header)) {
    return false;
  }

  (void)drainPendingConnectedControllerEvents();
  const BleCsSubeventResult& localResult = completedLocalResult();
  const BleCsSubeventResult& peerResult = completedPeerResult();
  const bool completedVprResultMatches =
      localResult.isComplete &&
      peerResult.isComplete &&
      localResult.header.connHandle == header.connHandle &&
      peerResult.header.connHandle == header.connHandle &&
      localResult.header.configId == header.configId &&
      peerResult.header.configId == header.configId &&
      localResult.header.procedureCounter == header.procedureCounter &&
      peerResult.header.procedureCounter == header.procedureCounter &&
      localResult.stepData != nullptr &&
      peerResult.stepData != nullptr &&
      localResult.stepDataLen > 0U &&
      peerResult.stepDataLen > 0U;
  if (completedVprResultMatches &&
      (host_.estimateValid() || host_.refreshEstimateFromCompletedResults())) {
    return true;
  }
  if (completedVprResultMatches) {
    size_t localStepDataLen = 0U;
    size_t peerStepDataLen = 0U;
    uint16_t localSteps = 0U;
    uint16_t peerSteps = 0U;
    BleCsEstimate measurementEstimate{};
    if (BleChannelSoundingRadio::encodeMode2StepDataFromMeasurements(
            measurements, count, false, localStepData, localMaxStepDataLen,
            &localStepDataLen, &localSteps) &&
        BleChannelSoundingRadio::encodeMode2StepDataFromMeasurements(
            measurements, count, true, peerStepData, peerMaxStepDataLen,
            &peerStepDataLen, &peerSteps) &&
        localSteps > 0U &&
        peerSteps > 0U &&
        BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
            localStepData, localStepDataLen, peerStepData, peerStepDataLen,
            true, &measurementEstimate) &&
        host_.applyEstimateToCompletedResults(measurementEstimate)) {
      return true;
    }
  }

  host_.resetProcedureRunState();
  return consumeMode2ControllerEventsFromMeasurements(
      measurements, count, header, localStepData, localMaxStepDataLen,
      peerStepData, peerMaxStepDataLen);
}

bool BleCsControllerVprHost::ready() const { return host_.ready(); }

bool BleCsControllerVprHost::failed() const { return host_.failed(); }

bool BleCsControllerVprHost::estimateValid() const { return host_.estimateValid(); }

bool BleCsControllerVprHost::refreshEstimateFromCompletedResults() {
  return host_.refreshEstimateFromCompletedResults();
}

uint8_t BleCsControllerVprHost::lastProcedureAbortReason() const {
  return host_.lastProcedureAbortReason();
}

uint8_t BleCsControllerVprHost::lastSubeventAbortReason() const {
  return host_.lastSubeventAbortReason();
}

const BleCsControllerVprHostState& BleCsControllerVprHost::vprState() const {
  return vprState_;
}

const BleCsControllerStreamHostState& BleCsControllerVprHost::streamState() const {
  return host_.state();
}

const BleCsControllerHostState& BleCsControllerVprHost::hostState() const {
  return host_.hostState();
}

const BleCsControllerSessionState& BleCsControllerVprHost::sessionState() const {
  return host_.sessionState();
}

const BleCsControllerWorkflowState& BleCsControllerVprHost::workflowState() const {
  return host_.workflowState();
}

const BleCsControllerVprDrainStats& BleCsControllerVprHost::lastDrainStats() const {
  return lastDrainStats_;
}

const BleCsSubeventResult& BleCsControllerVprHost::localResult() const {
  return host_.localResult();
}

const BleCsSubeventResult& BleCsControllerVprHost::peerResult() const {
  return host_.peerResult();
}

const BleCsSubeventResult& BleCsControllerVprHost::completedLocalResult() const {
  return host_.completedLocalResult();
}

const BleCsSubeventResult& BleCsControllerVprHost::completedPeerResult() const {
  return host_.completedPeerResult();
}

bool BleCsControllerVprHost::lastRemoteFaeTableValid() const {
  return lastRemoteFaeTable_.valid;
}

const BleCsFaeTable& BleCsControllerVprHost::lastRemoteFaeTable() const {
  return lastRemoteFaeTable_;
}

bool BleCsControllerVprHost::lastTestEndCompleteValid() const {
  return lastTestEndCompleteValid_;
}

const BleCsTestEndComplete& BleCsControllerVprHost::lastTestEndComplete() const {
  return lastTestEndComplete_;
}

bool BleCsControllerVprHost::lastTestResultValid() const {
  return lastTestResultValid_;
}

const BleCsSubeventResult& BleCsControllerVprHost::lastTestResult() const {
  return lastTestResult_;
}

uint16_t BleCsControllerVprHost::testResultCount() const {
  return testResultCount_;
}

bool BleCsControllerVprHost::cachedRemoteCapabilitiesV1(
    BleCsControllerCapabilities* outCapabilities) const {
  if (outCapabilities == nullptr || !cachedRemoteCapabilitiesV1Valid_) {
    return false;
  }
  *outCapabilities = cachedRemoteCapabilitiesV1_;
  return true;
}

bool BleCsControllerVprHost::cachedRemoteCapabilitiesV2(
    BleCsControllerCapabilities* outCapabilities) const {
  if (outCapabilities == nullptr || !cachedRemoteCapabilitiesV2Valid_) {
    return false;
  }
  *outCapabilities = cachedRemoteCapabilitiesV2_;
  return true;
}

VprSharedTransportStream& BleCsControllerVprHost::transport() { return transport_; }

const VprSharedTransportStream& BleCsControllerVprHost::transport() const {
  return transport_;
}

bool BleCsControllerVprHost::consumeDirectAuxiliaryEvent(
    const uint8_t* packet,
    size_t packetLen) {
  BleCsHciCommandStatusEvent statusEvent{};
  if (BleChannelSoundingRadio::parseHciCommandStatusEvent(
          packet, packetLen, &statusEvent)) {
    switch (statusEvent.opcode) {
      case kBleCsHciOpWriteCachedRemoteSupportedCapabilities:
      case kBleCsHciOpReadRemoteFaeTable:
      case kBleCsHciOpWriteCachedRemoteFaeTable:
      case kBleCsHciOpSetChannelClassification:
      case kBleCsHciOpTest:
      case kBleCsHciOpTestEnd:
      case kBleCsHciOpWriteCachedRemoteSupportedCapabilitiesV2:
        return true;
      default:
        return false;
    }
  }

  BleCsHciCommandCompleteEvent completeEvent{};
  if (BleChannelSoundingRadio::parseHciCommandCompleteEvent(
          packet, packetLen, &completeEvent)) {
    switch (completeEvent.opcode) {
      case kBleCsHciOpWriteCachedRemoteSupportedCapabilities:
      case kBleCsHciOpReadRemoteFaeTable:
      case kBleCsHciOpWriteCachedRemoteFaeTable:
      case kBleCsHciOpSetChannelClassification:
      case kBleCsHciOpTest:
      case kBleCsHciOpTestEnd:
      case kBleCsHciOpWriteCachedRemoteSupportedCapabilitiesV2:
      case kBleCsVprHciOpMeasurementExecute:
      case kBleCsVprHciOpMeasurementSnapshotRead:
        return true;
      default:
        return false;
    }
  }

  BleCsHciLeMetaEvent metaEvent{};
  if (!BleChannelSoundingRadio::parseHciLeMetaEvent(
          packet, packetLen, &metaEvent)) {
    return false;
  }
  if (metaEvent.subeventCode == kBleCsHciEvtReadRemoteFaeTableComplete) {
    return BleChannelSoundingRadio::parseHciReadRemoteFaeTableCompleteEvent(
        metaEvent.payload, metaEvent.payloadLen, &lastRemoteFaeTable_);
  }
  if (metaEvent.subeventCode == kBleCsHciEvtReadRemoteSupportedCapabilitiesComplete) {
    BleCsControllerCapabilities caps{};
    if (!BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteEvent(
            metaEvent.payload, metaEvent.payloadLen, &caps)) {
      return false;
    }
    cachedRemoteCapabilitiesV1_ = caps;
    cachedRemoteCapabilitiesV1Valid_ = caps.valid;
    return true;
  }
  if (metaEvent.subeventCode == kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2) {
    BleCsControllerCapabilities caps{};
    if (!BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteV2Event(
            metaEvent.payload, metaEvent.payloadLen, &caps)) {
      return false;
    }
    cachedRemoteCapabilitiesV2_ = caps;
    cachedRemoteCapabilitiesV2Valid_ = caps.valid;
    return true;
  }
  if (metaEvent.subeventCode == kBleCsHciEvtTestEndComplete) {
    BleCsTestEndComplete complete{};
    if (!BleChannelSoundingRadio::parseHciTestEndCompleteEvent(
            metaEvent.payload, metaEvent.payloadLen, &complete)) {
      return false;
    }
    lastTestEndComplete_ = complete;
    lastTestEndCompleteValid_ = true;
    return true;
  }
  if (metaEvent.subeventCode == kBleCsHciEvtSubeventResult ||
      metaEvent.subeventCode == kBleCsHciEvtSubeventResultContinue) {
    /* Standalone CS Test results arrive on the reserved 0x0FFF handle and are
     * collected here, independent of the connected-procedure session. Connected
     * handles fall through (return false) so host_.consumeControllerPacket keeps
     * owning them. */
    BleCsSubeventResult probe{};
    const bool isInitial = (metaEvent.subeventCode == kBleCsHciEvtSubeventResult);
    const bool parsed = isInitial
        ? BleChannelSoundingRadio::parseHciSubeventResultEvent(
              metaEvent.payload, metaEvent.payloadLen, &probe)
        : BleChannelSoundingRadio::parseHciSubeventResultContinueEvent(
              metaEvent.payload, metaEvent.payloadLen, &probe);
    if (parsed && probe.header.connHandle == kBleCsHciTestConnHandle) {
      return consumeTestResultEvent(metaEvent.subeventCode, metaEvent.payload,
                                    metaEvent.payloadLen);
    }
    return false;
  }
  return false;
}

bool BleCsControllerVprHost::consumeTestResultEvent(uint8_t subeventCode,
                                                    const uint8_t* payload,
                                                    size_t payloadLen) {
  BleCsSubeventResult result{};
  bool ok = false;
  if (subeventCode == kBleCsHciEvtSubeventResult) {
    ok = BleChannelSoundingRadio::parseHciSubeventResultEvent(payload, payloadLen, &result);
  } else if (subeventCode == kBleCsHciEvtSubeventResultContinue) {
    ok = BleChannelSoundingRadio::parseHciSubeventResultContinueEvent(payload, payloadLen,
                                                                       &result);
  } else {
    return false;
  }
  if (!ok || result.header.connHandle != kBleCsHciTestConnHandle) {
    return false;
  }
  if (subeventCode == kBleCsHciEvtSubeventResult) {
    ok = testReassembler_.consumeInitialEvent(payload, payloadLen, &lastTestResult_);
  } else {
    ok = testReassembler_.consumeContinuationEvent(payload, payloadLen, &lastTestResult_);
  }
  if (!ok) {
    return false;
  }
  lastTestResultValid_ = true;
  if (lastTestResult_.isComplete &&
      lastTestResult_.header.procedureDoneStatus != kBleCsProcedureDonePartial) {
    testResultCount_ = static_cast<uint16_t>(testResultCount_ + 1U);
  }
  return true;
}

bool BleCsControllerVprHost::drainDirectControllerEvents(VprControllerServiceHost* directHost,
                                                         const uint8_t* response,
                                                         size_t responseLen,
                                                         bool waitForBackgroundResults,
                                                         bool requireConnectedCsResult) {
  lastDrainStats_ = BleCsControllerVprDrainStats{};
  if (directHost == nullptr || !host_.hostState().began) {
    return !requireConnectedCsResult;
  }

  const BleCsControllerHostState baseline = host_.hostState();
  const auto connectedCsResultArrived = [&]() -> bool {
    if (!requireConnectedCsResult) {
      return false;
    }
    const BleCsControllerHostState& state = host_.hostState();
    return state.localResultPackets > baseline.localResultPackets &&
           state.peerResultPackets > baseline.peerResultPackets &&
           state.controllerPeerResultMarkers >
               baseline.controllerPeerResultMarkers &&
           host_.estimateValid();
  };
  const auto consumeDirectPacket = [&](const uint8_t* packet,
                                      size_t packetLen) -> void {
    ++lastDrainStats_.packetsRead;
    lastDrainStats_.lastPacketLen =
        (packetLen > 0xFFFFU) ? 0xFFFFU : static_cast<uint16_t>(packetLen);
    lastDrainStats_.lastEventCode = 0U;
    lastDrainStats_.lastLeSubeventCode = 0U;
    lastDrainStats_.lastVendorSubeventCode = 0U;
    if (packet != nullptr && packetLen >= 3U && packet[0] == 0x04U) {
      lastDrainStats_.lastEventCode = packet[1];
      if (packet[1] == kBleHciEvtLeMeta && packetLen >= 4U) {
        lastDrainStats_.lastLeSubeventCode = packet[3];
      } else if (packet[1] == kBleHciEvtVendor && packetLen >= 4U) {
        lastDrainStats_.lastVendorSubeventCode = packet[3];
      }
    }
    BleCsSubeventResult parsedDirectResult{};
    bool parsedDirectResultValid = false;
    BleCsHciLeMetaEvent parsedDirectMeta{};
    if (BleChannelSoundingRadio::parseHciLeMetaEvent(packet, packetLen,
                                                     &parsedDirectMeta)) {
      if (parsedDirectMeta.subeventCode == kBleCsHciEvtSubeventResult) {
        parsedDirectResultValid =
            BleChannelSoundingRadio::parseHciSubeventResultEvent(
                parsedDirectMeta.payload, parsedDirectMeta.payloadLen,
                &parsedDirectResult);
      } else if (parsedDirectMeta.subeventCode ==
                 kBleCsHciEvtSubeventResultContinue) {
        parsedDirectResultValid =
            BleChannelSoundingRadio::parseHciSubeventResultContinueEvent(
                parsedDirectMeta.payload, parsedDirectMeta.payloadLen,
                &parsedDirectResult);
      }
    }
    const bool consumed =
        consumeDirectAuxiliaryEvent(packet, packetLen) ||
        host_.consumeControllerPacket(packet, packetLen);
    if (consumed) {
      ++lastDrainStats_.packetsConsumed;
    } else {
      ++lastDrainStats_.packetsRejected;
      if (lastDrainStats_.firstRejectedEventCode == 0U) {
        lastDrainStats_.firstRejectedPacketLen =
            (packetLen > 0xFFFFU) ? 0xFFFFU : static_cast<uint16_t>(packetLen);
        lastDrainStats_.firstRejectedEventCode = lastDrainStats_.lastEventCode;
        lastDrainStats_.firstRejectedLeSubeventCode =
            lastDrainStats_.lastLeSubeventCode;
        if (parsedDirectResultValid) {
          lastDrainStats_.firstRejectedConnHandle =
              parsedDirectResult.header.connHandle;
          lastDrainStats_.firstRejectedConfigId =
              parsedDirectResult.header.configId;
          lastDrainStats_.firstRejectedProcedureCounter =
              parsedDirectResult.header.procedureCounter;
          lastDrainStats_.firstRejectedSteps =
              static_cast<uint8_t>(parsedDirectResult.header.numStepsReported &
                                   0xFFU);
          lastDrainStats_.firstRejectedProcedureDoneStatus =
              parsedDirectResult.header.procedureDoneStatus;
          lastDrainStats_.firstRejectedSubeventDoneStatus =
              parsedDirectResult.header.subeventDoneStatus;
        }
      }
      if (parsedDirectResultValid &&
          lastDrainStats_.firstRejectedResultPacketLen == 0U) {
        lastDrainStats_.firstRejectedResultPacketLen =
            (packetLen > 0xFFFFU) ? 0xFFFFU : static_cast<uint16_t>(packetLen);
        lastDrainStats_.firstRejectedResultLeSubeventCode =
            lastDrainStats_.lastLeSubeventCode;
        lastDrainStats_.firstRejectedResultConnHandle =
            parsedDirectResult.header.connHandle;
        lastDrainStats_.firstRejectedResultConfigId =
            parsedDirectResult.header.configId;
        lastDrainStats_.firstRejectedResultProcedureCounter =
            parsedDirectResult.header.procedureCounter;
        lastDrainStats_.firstRejectedResultSteps =
            static_cast<uint8_t>(parsedDirectResult.header.numStepsReported &
                                 0xFFU);
        lastDrainStats_.firstRejectedResultProcedureDoneStatus =
            parsedDirectResult.header.procedureDoneStatus;
        lastDrainStats_.firstRejectedResultSubeventDoneStatus =
            parsedDirectResult.header.subeventDoneStatus;
      }
    }
  };

  if (response != nullptr && responseLen > 0U) {
    // Direct VPR HCI may return command-complete or CS test packets that are
    // valid for the direct caller but intentionally invisible to the public host.
    consumeDirectPacket(response, responseLen);
    if (connectedCsResultArrived()) {
      return true;
    }
  }

  uint8_t packet[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA] = {0};
  size_t packetLen = 0U;
  while (directHost->popPendingH4Event(packet, sizeof(packet), &packetLen)) {
    ++lastDrainStats_.pendingPacketsPopped;
    consumeDirectPacket(packet, packetLen);
    if (connectedCsResultArrived()) {
      return true;
    }
  }

  uint16_t pollCount = 0U;
  while (transport_.available() > 0 && pollCount < 8U) {
    packetLen = 0U;
    if (!directHost->readNextH4Event(packet, sizeof(packet), &packetLen, 20U)) {
      ++lastDrainStats_.readFailures;
      return false;
    }
    consumeDirectPacket(packet, packetLen);
    ++pollCount;
    if (connectedCsResultArrived()) {
      return true;
    }
  }
  /* Clear vprFlags=PENDING in shared memory so the VPR main loop can
   * produce background results (e.g. CS test stream). pullResponse() is
   * called by poll() which is called by available(). */
  (void)transport_.available();
  if (!waitForBackgroundResults || !requireConnectedCsResult) {
    return !requireConnectedCsResult || connectedCsResultArrived();
  }
  /* Busy-wait for the VPR to produce background results. The VPR runs
   * at ~1kHz heartbeat; poll every 2ms. Measurement-execute has a stricter
   * counter target because the direct command completion only means the VPR
   * armed the result producer, not that local/peer packets were consumed. */
  const uint32_t waitStart = millis();
  const uint32_t waitLimitMs = requireConnectedCsResult ? 350UL : 100UL;
  while ((millis() - waitStart) < waitLimitMs) {
    (void)transport_.available();  /* poll -> pullResponse -> clear PENDING */
    while (transport_.available() > 0 && pollCount < 128U) {
      packetLen = 0U;
      if (!directHost->readNextH4Event(packet, sizeof(packet), &packetLen, 20U)) {
        ++lastDrainStats_.readFailures;
        return false;
      }
      consumeDirectPacket(packet, packetLen);
      ++pollCount;
      if (connectedCsResultArrived()) {
        return true;
      }
    }
    if (connectedCsResultArrived()) {
      return true;
    }
    delay(2);
  }
  (void)transport_.available();
  return !requireConnectedCsResult || connectedCsResultArrived();
}

void BleCsControllerVprHost::syncVprState() {
  const BleCsControllerVprHostState previous = vprState_;
  BleCsControllerVprHostState nextState = previous;
  nextState.heartbeat = transport_.heartbeat();
  nextState.lastOpcode = transport_.lastOpcode();
  nextState.transportStatus = transport_.transportStatus();
  nextState.lastError = transport_.lastError();
  nextState.running = transport_.isRunning();
  nextState.secureAccessEnabled = transport_.secureAccessEnabled();
  const uint32_t packedLinkState = transport_.reservedState();
  const uint32_t packedAuxState = transport_.reservedAuxState();
  const uint32_t packedMetaState = transport_.reservedMetaState();
  const uint32_t packedConfigState = transport_.reservedConfigState();
  const uint8_t slotFlags = static_cast<uint8_t>((packedMetaState >> 24U) & 0xFFU);
  nextState.linkConnHandle = static_cast<uint16_t>(packedLinkState & 0x0FFFU);
  nextState.linkProcedureIntervalSelector =
      static_cast<uint8_t>((packedLinkState >> 12U) & 0x0FU);
  nextState.linkStoredConfigCount = static_cast<uint8_t>(packedAuxState & 0x0FU);
  nextState.linkResultPublishReason =
      static_cast<uint8_t>((packedAuxState >> 4U) & 0x0FU);
  nextState.linkPeerGapTicks = static_cast<uint8_t>((packedAuxState >> 8U) & 0x0FU);
  nextState.linkResultPendingStage =
      static_cast<uint8_t>((packedAuxState >> 12U) & 0x0FU);
  nextState.linkLastEvictedConfigId = static_cast<uint8_t>((packedAuxState >> 16U) & 0xFFU);
  nextState.linkAuthority2ConfigId = static_cast<uint8_t>((packedAuxState >> 24U) & 0xFFU);
  nextState.linkSessionOpen = (packedLinkState & (1UL << 16U)) != 0U;
  nextState.linkConfigCreated = (packedLinkState & (1UL << 17U)) != 0U;
  nextState.linkSecurityEnabled = (packedLinkState & (1UL << 18U)) != 0U;
  nextState.linkProcedureParamsApplied = (packedLinkState & (1UL << 19U)) != 0U;
  nextState.linkProcedureEnabled = (packedLinkState & (1UL << 20U)) != 0U;
  nextState.linkConfigId = static_cast<uint8_t>((packedLinkState >> 21U) & 0xFFU);
  nextState.linkSlot0ConfigId = static_cast<uint8_t>(packedMetaState & 0xFFU);
  nextState.linkSlot1ConfigId = static_cast<uint8_t>((packedMetaState >> 8U) & 0xFFU);
  nextState.linkPreviousConfigId = static_cast<uint8_t>((packedMetaState >> 16U) & 0xFFU);
  nextState.linkAuthority0ConfigId = static_cast<uint8_t>((packedConfigState >> 12U) & 0xFFU);
  nextState.linkAuthority1ConfigId = static_cast<uint8_t>((packedConfigState >> 20U) & 0xFFU);
  nextState.linkSlot0InUse = (slotFlags & 0x01U) != 0U;
  nextState.linkSlot1InUse = (slotFlags & 0x02U) != 0U;
  nextState.linkPreviousSlotInUse = (slotFlags & 0x04U) != 0U;
  nextState.linkActivePrimarySlotIndex = 0xFFU;
  if ((slotFlags & 0x08U) != 0U) {
    nextState.linkActivePrimarySlotIndex = 0U;
  } else if ((slotFlags & 0x10U) != 0U) {
    nextState.linkActivePrimarySlotIndex = 1U;
  }
  nextState.linkActiveConfigMirroredInPrevious = (slotFlags & 0x20U) != 0U;
  nextState.linkFreePrimarySlotCount = static_cast<uint8_t>((slotFlags >> 6U) & 0x03U);
  nextState.linkSlot0Runnable = (packedConfigState & 0x01U) != 0U;
  nextState.linkSlot1Runnable = (packedConfigState & 0x02U) != 0U;
  nextState.linkPreviousSlotRunnable = (packedConfigState & 0x04U) != 0U;
  nextState.linkSelectedConfigRunnable = (packedConfigState & 0x08U) != 0U;
  nextState.linkSlot0SecurityEnabled = (packedConfigState & 0x10U) != 0U;
  nextState.linkSlot1SecurityEnabled = (packedConfigState & 0x20U) != 0U;
  nextState.linkPreviousSlotSecurityEnabled = (packedConfigState & 0x40U) != 0U;
  nextState.linkSelectedConfigSecurityEnabled = (packedConfigState & 0x80U) != 0U;
  nextState.linkSlot0ProcedureParamsApplied = (packedConfigState & 0x100U) != 0U;
  nextState.linkSlot1ProcedureParamsApplied = (packedConfigState & 0x200U) != 0U;
  nextState.linkPreviousSlotProcedureParamsApplied = (packedConfigState & 0x400U) != 0U;
  nextState.linkSelectedConfigProcedureParamsApplied = (packedConfigState & 0x800U) != 0U;
  nextState.linkMeasurementExecuteResultActive =
      (packedConfigState & (1UL << 28U)) != 0U;
  nextState.linkResultPublishedStage =
      static_cast<uint8_t>((packedConfigState >> 29U) & 0x07U);
  nextState.linkProcedureCounter = nextState.scheduler.procedureCounter;
  vprState_ = nextState;

  const bool linkSessionInvalidated =
      (previous.linkSessionOpen && !nextState.linkSessionOpen) ||
      (previous.linkConnHandle != 0U && previous.linkConnHandle != nextState.linkConnHandle);
  const bool linkConfigInvalidated =
      (previous.linkConfigCreated && !nextState.linkConfigCreated) ||
      (previous.linkConfigId != 0U && previous.linkConfigId != nextState.linkConfigId);
  if (linkSessionInvalidated || linkConfigInvalidated) {
    host_.resetProcedureRunState();
  }
  if (linkSessionInvalidated) {
    handleDisconnect();
  }
  host_.reconcileReadyWorkflowShadow(nextState.linkConfigId, nextState.linkSessionOpen,
                                     nextState.linkConfigCreated,
                                     nextState.linkSecurityEnabled,
                                     nextState.linkProcedureParamsApplied,
                                     nextState.linkProcedureEnabled);
}

BleCsDfeCaptureInfo BleChannelSoundingRadio::lastDfeCaptureInfo() const {
  BleCsDfeCaptureInfo info{};
  info.present = (lastDfePacketAmountBytes_ > 0U);
  info.allZero = lastDfePacketAllZero_;
  info.amountBytes = lastDfePacketAmountBytes_;
  info.currentAmountBytes = lastDfePacketCurrentAmountBytes_;
  return info;
}

bool BleChannelSoundingRadio::copyLastDfePacket(uint8_t* outPacket,
                                                size_t maxLen,
                                                size_t* outLen) const {
  const size_t available =
      (lastDfePacketAmountBytes_ <= sizeof(dfePacket_))
          ? lastDfePacketAmountBytes_
          : sizeof(dfePacket_);
  const size_t copyLen = (available <= maxLen) ? available : maxLen;
  if (outLen != nullptr) {
    *outLen = copyLen;
  }
  if (outPacket == nullptr) {
    return false;
  }
  if (copyLen > 0U) {
    memcpy(outPacket, dfePacket_, copyLen);
  }
  return (copyLen == available);
}

bool BleChannelSoundingRadio::encodePeerSessionRequest(
    const BleCsPeerSession& session,
    uint8_t* outData,
    size_t maxDataLen,
    uint16_t* outDataLen) {
  if (outDataLen != nullptr) {
    *outDataLen = 0U;
  }
  if (outData == nullptr || outDataLen == nullptr || session.token == 0U ||
      session.profileTag == 0U ||
      maxDataLen < kBleCsPeerSessionRequestBytes) {
    return false;
  }

  memset(outData, 0, kBleCsPeerSessionRequestBytes);
  writeLe32(outData + 0U, kPeerSessionMagic);
  outData[4U] = kPeerProtocolVersion;
  writeLe16(outData + 6U, session.drbgNonce);
  writeLe32(outData + 8U, session.token);
  writeLe32(outData + 12U, session.profileTag);
  *outDataLen = static_cast<uint16_t>(kBleCsPeerSessionRequestBytes);
  return true;
}

bool BleChannelSoundingRadio::decodePeerSessionRequest(
    const uint8_t* data,
    size_t dataLen,
    BleCsPeerSession* outSession) {
  if (outSession != nullptr) {
    *outSession = BleCsPeerSession{};
  }
  if (data == nullptr || outSession == nullptr ||
      dataLen != kBleCsPeerSessionRequestBytes ||
      readLe32(data + 0U) != kPeerSessionMagic ||
      data[4U] != kPeerProtocolVersion) {
    return false;
  }

  BleCsPeerSession session{};
  session.drbgNonce = readLe16(data + 6U);
  session.token = readLe32(data + 8U);
  session.profileTag = readLe32(data + 12U);
  if (session.token == 0U || session.profileTag == 0U) {
    return false;
  }
  *outSession = session;
  return true;
}

bool BleChannelSoundingRadio::encodePeerResultEnvelope(
    const BleCsPeerResultMetadata& metadata,
    const uint8_t* stepData,
    uint16_t stepDataLen,
    uint8_t* outData,
    size_t maxDataLen,
    uint16_t* outDataLen) {
  if (outDataLen != nullptr) {
    *outDataLen = 0U;
  }
  const size_t totalLen = kBleCsPeerResultEnvelopeHeaderBytes + stepDataLen;
  if (outData == nullptr || outDataLen == nullptr || stepData == nullptr ||
      stepDataLen == 0U || metadata.session.token == 0U ||
      metadata.session.profileTag == 0U || metadata.role > 1U ||
      metadata.numAntennaPaths > 1U || metadata.numStepsReported == 0U ||
      totalLen > maxDataLen || totalLen > kBleCsMaxControllerStepDataBytes) {
    return false;
  }

  memset(outData, 0, kBleCsPeerResultEnvelopeHeaderBytes);
  writeLe32(outData + 0U, kPeerResultMagic);
  outData[4U] = kPeerProtocolVersion;
  outData[5U] = metadata.role;
  outData[6U] = metadata.configId;
  outData[7U] = metadata.numAntennaPaths;
  writeLe16(outData + 8U, metadata.startAclConnEventCounter);
  writeLe16(outData + 10U, metadata.procedureCounter);
  writeLe16(outData + 12U, metadata.numStepsReported);
  writeLe16(outData + 14U, stepDataLen);
  writeLe32(outData + 16U, metadata.session.token);
  writeLe32(outData + 20U, metadata.session.profileTag);
  writeLe16(outData + 24U, metadata.session.drbgNonce);
  outData[26U] = metadata.mainModeType;
  outData[27U] = metadata.subModeType;
  outData[28U] = metadata.rttType;
  memcpy(outData + kBleCsPeerResultEnvelopeHeaderBytes, stepData,
         stepDataLen);
  *outDataLen = static_cast<uint16_t>(totalLen);
  return true;
}

bool BleChannelSoundingRadio::decodePeerResultEnvelope(
    const uint8_t* data,
    size_t dataLen,
    BleCsPeerResultMetadata* outMetadata,
    const uint8_t** outStepData,
    uint16_t* outStepDataLen) {
  if (outMetadata != nullptr) {
    *outMetadata = BleCsPeerResultMetadata{};
  }
  if (outStepData != nullptr) {
    *outStepData = nullptr;
  }
  if (outStepDataLen != nullptr) {
    *outStepDataLen = 0U;
  }
  if (data == nullptr || outMetadata == nullptr || outStepData == nullptr ||
      outStepDataLen == nullptr ||
      dataLen < kBleCsPeerResultEnvelopeHeaderBytes ||
      readLe32(data + 0U) != kPeerResultMagic ||
      data[4U] != kPeerProtocolVersion) {
    return false;
  }

  BleCsPeerResultMetadata metadata{};
  metadata.role = data[5U];
  metadata.configId = data[6U];
  metadata.numAntennaPaths = data[7U];
  metadata.startAclConnEventCounter = readLe16(data + 8U);
  metadata.procedureCounter = readLe16(data + 10U);
  metadata.numStepsReported = readLe16(data + 12U);
  const uint16_t stepDataLen = readLe16(data + 14U);
  metadata.session.token = readLe32(data + 16U);
  metadata.session.profileTag = readLe32(data + 20U);
  metadata.session.drbgNonce = readLe16(data + 24U);
  metadata.mainModeType = data[26U];
  metadata.subModeType = data[27U];
  metadata.rttType = data[28U];
  const size_t expectedLen =
      kBleCsPeerResultEnvelopeHeaderBytes + stepDataLen;
  if (metadata.role > 1U || metadata.numAntennaPaths > 1U ||
      metadata.numStepsReported == 0U || stepDataLen == 0U ||
      metadata.session.token == 0U || metadata.session.profileTag == 0U ||
      expectedLen != dataLen || dataLen > kBleCsMaxControllerStepDataBytes) {
    return false;
  }

  *outMetadata = metadata;
  *outStepData = data + kBleCsPeerResultEnvelopeHeaderBytes;
  *outStepDataLen = stepDataLen;
  return true;
}

bool BleChannelSoundingRadio::sendPeerStepData(
    const uint8_t* stepData,
    uint16_t stepDataLen,
    uint16_t transferId,
    uint32_t timeoutMs,
    BleCsStepTransferStats* outStats) {
  BleCsStepTransferStats stats{};
  stats.transferId = transferId;
  if (outStats != nullptr) {
    *outStats = stats;
  }
  if (!initialized_ || stepData == nullptr || stepDataLen == 0U ||
      stepDataLen > kBleCsMaxControllerStepDataBytes || timeoutMs == 0U ||
      config_.maxPayloadLength <=
          (kPayloadHeaderLen + kStepTransferHeaderLen)) {
    return false;
  }

  const uint8_t payloadCapacity = static_cast<uint8_t>(
      config_.maxPayloadLength - kPayloadHeaderLen - kStepTransferHeaderLen);
  const uint8_t chunkCapacity =
      (payloadCapacity < kStepTransferChunkLimit) ? payloadCapacity
                                                  : kStepTransferChunkLimit;
  if (chunkCapacity == 0U) {
    return false;
  }

  const uint32_t startedMs = millis();
  const uint32_t payloadCrc = crc32Bytes(stepData, stepDataLen);
  stats.payloadCrc32 = payloadCrc;
  uint16_t offset = 0U;
  uint8_t sequence = static_cast<uint8_t>(transferId);
  while (offset < stepDataLen && (millis() - startedMs) < timeoutMs) {
    const uint16_t remaining = static_cast<uint16_t>(stepDataLen - offset);
    const uint8_t chunkLen = static_cast<uint8_t>(
        (remaining < chunkCapacity) ? remaining : chunkCapacity);
    uint8_t extra[kStepTransferHeaderLen + kStepTransferChunkLimit] = {0};
    extra[0] = kStepTransferVersion;
    writeLe16(&extra[1], transferId);
    writeLe16(&extra[3], stepDataLen);
    writeLe16(&extra[5], offset);
    writeLe32(&extra[7], payloadCrc);
    memcpy(&extra[kStepTransferHeaderLen], &stepData[offset], chunkLen);

    bool acknowledged = false;
    bool restartRequested = false;
    while (!acknowledged && (millis() - startedMs) < timeoutMs) {
      const uint8_t flags =
          (static_cast<uint16_t>(offset + chunkLen) == stepDataLen) ? 0x01U
                                                                    : 0x00U;
      if (!sendFrame(kStepTransferChannel, PacketType::kStepData, sequence,
                     kStepTransferChannel, flags, extra,
                     static_cast<uint8_t>(kStepTransferHeaderLen + chunkLen),
                     false, false)) {
        ++stats.retries;
        continue;
      }
      ++stats.framesSent;

      RxFrame ack{};
      if (!receiveFrame(kStepTransferChannel, kStepTransferListenWindowUs,
                        false, false, false, &ack, nullptr, nullptr) ||
          !ack.valid || ack.type != PacketType::kStepAck ||
          ack.sequence != sequence || ack.extraLen != kStepAckLen ||
          ack.extra[0] != kStepTransferVersion ||
          readLe16(&ack.extra[1]) != transferId) {
        ++stats.retries;
        continue;
      }

      const uint16_t acknowledgedOffset = readLe16(&ack.extra[3]);
      if (ack.extra[5] == 2U && acknowledgedOffset == 0U) {
        ++stats.retries;
        offset = 0U;
        stats.bytesTransferred = 0U;
        sequence = static_cast<uint8_t>(transferId);
        restartRequested = true;
        break;
      }
      if (ack.extra[5] != 0U ||
          acknowledgedOffset !=
              static_cast<uint16_t>(offset + chunkLen)) {
        ++stats.retries;
        continue;
      }
      ++stats.acknowledgements;
      acknowledged = true;
    }

    if (restartRequested) {
      continue;
    }
    if (!acknowledged) {
      if (outStats != nullptr) {
        *outStats = stats;
      }
      return false;
    }
    offset = static_cast<uint16_t>(offset + chunkLen);
    stats.bytesTransferred = offset;
    ++sequence;
  }

  const bool complete = (offset == stepDataLen);
  if (outStats != nullptr) {
    *outStats = stats;
  }
  return complete;
}

bool BleChannelSoundingRadio::receivePeerStepData(
    uint8_t* outStepData,
    size_t maxStepDataLen,
    uint16_t* outStepDataLen,
    uint16_t* outTransferId,
    uint32_t timeoutMs,
    BleCsStepTransferStats* outStats) {
  BleCsStepTransferStats stats{};
  if (outStepDataLen != nullptr) {
    *outStepDataLen = 0U;
  }
  if (outTransferId != nullptr) {
    *outTransferId = 0U;
  }
  if (outStats != nullptr) {
    *outStats = stats;
  }
  if (!initialized_ || outStepData == nullptr || outStepDataLen == nullptr ||
      outTransferId == nullptr || maxStepDataLen == 0U ||
      maxStepDataLen > kBleCsMaxControllerStepDataBytes || timeoutMs == 0U) {
    return false;
  }

  const uint32_t startedMs = millis();
  bool transferStarted = false;
  uint16_t transferId = 0U;
  uint16_t totalLen = 0U;
  uint16_t receivedLen = 0U;
  uint32_t expectedCrc = 0U;
  uint32_t completionObservedMs = 0U;
  bool completionObserved = false;
  bool finalAckSent = false;
  while ((millis() - startedMs) < timeoutMs) {
    if (completionObserved &&
        (millis() - completionObservedMs) >=
            kStepTransferFinalAckLingerMs) {
      break;
    }

    RxFrame frame{};
    if (!receiveFrame(kStepTransferChannel, kStepTransferListenWindowUs,
                      false, false, false, &frame, nullptr, nullptr)) {
      continue;
    }
    ++stats.framesReceived;
    if (!frame.valid || frame.type != PacketType::kStepData ||
        frame.extraLen <= kStepTransferHeaderLen ||
        frame.extra[0] != kStepTransferVersion) {
      ++stats.rejectedFrames;
      continue;
    }

    const uint16_t frameTransferId = readLe16(&frame.extra[1]);
    const uint16_t frameTotalLen = readLe16(&frame.extra[3]);
    const uint16_t frameOffset = readLe16(&frame.extra[5]);
    const uint32_t frameCrc = readLe32(&frame.extra[7]);
    const uint8_t chunkLen =
        static_cast<uint8_t>(frame.extraLen - kStepTransferHeaderLen);
    const uint32_t frameEnd =
        static_cast<uint32_t>(frameOffset) + static_cast<uint32_t>(chunkLen);

    if (frameTotalLen == 0U || frameTotalLen > maxStepDataLen ||
        frameTotalLen > kBleCsMaxControllerStepDataBytes ||
        frameEnd > frameTotalLen) {
      ++stats.rejectedFrames;
      continue;
    }
    if (!transferStarted) {
      if (frameOffset != 0U) {
        ++stats.rejectedFrames;
        continue;
      }
      transferStarted = true;
      transferId = frameTransferId;
      totalLen = frameTotalLen;
      expectedCrc = frameCrc;
      stats.transferId = transferId;
      stats.payloadCrc32 = expectedCrc;
    }
    if (frameTransferId != transferId || frameTotalLen != totalLen ||
        frameCrc != expectedCrc) {
      ++stats.rejectedFrames;
      continue;
    }

    uint8_t ackStatus = 0U;
    if (frameOffset == receivedLen) {
      memcpy(&outStepData[receivedLen],
             &frame.extra[kStepTransferHeaderLen], chunkLen);
      receivedLen = static_cast<uint16_t>(receivedLen + chunkLen);
    } else if (frameOffset < receivedLen && frameEnd <= receivedLen) {
      ++stats.duplicateFrames;
    } else {
      ++stats.rejectedFrames;
      ackStatus = 1U;
    }

    if (receivedLen == totalLen &&
        crc32Bytes(outStepData, receivedLen) != expectedCrc) {
      ++stats.rejectedFrames;
      receivedLen = 0U;
      ackStatus = 2U;
      transferStarted = false;
    }

    uint8_t ack[kStepAckLen] = {0};
    ack[0] = kStepTransferVersion;
    writeLe16(&ack[1], transferId);
    writeLe16(&ack[3], receivedLen);
    ack[5] = ackStatus;
    waitElapsedMicros(kStepTransferAckTurnaroundUs);
    if (sendFrame(kStepTransferChannel, PacketType::kStepAck, frame.sequence,
                  kStepTransferChannel, 0U, ack, sizeof(ack), false, false)) {
      ++stats.framesSent;
      ++stats.acknowledgements;
      if (ackStatus == 0U && receivedLen == totalLen) {
        finalAckSent = true;
      }
    }

    if (ackStatus == 0U && receivedLen == totalLen && finalAckSent) {
      if (!completionObserved) {
        completionObserved = true;
        completionObservedMs = millis();
      }
    }
  }

  stats.bytesTransferred = receivedLen;
  const bool complete = completionObserved && finalAckSent &&
                        receivedLen == totalLen;
  if (complete) {
    *outStepDataLen = receivedLen;
    *outTransferId = transferId;
  }
  if (outStats != nullptr) {
    *outStats = stats;
  }
  return complete;
}

bool BleChannelSoundingRadio::configureBle2MCommon() {
  radio_->SHORTS = 0U;
  detachRawRadioAutomation(radio_);
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  if (!waitForRadioDisabled(radio_, kRadioDisableBudgetUs)) {
    return false;
  }
  clearRadioEvents(radio_);
  radio_->TASKS_SOFTRESET = RADIO_TASKS_SOFTRESET_TASKS_SOFTRESET_Trigger;
  detachRawRadioAutomation(radio_);
  clearRadioEvents(radio_);

  radio_->MODE = ((RADIO_MODE_MODE_Ble_2Mbit << RADIO_MODE_MODE_Pos) &
                  RADIO_MODE_MODE_Msk);
  radio_->TIMING = ((RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) &
                    RADIO_TIMING_RU_Msk);

  uint32_t pcnf0 = 0U;
  pcnf0 |= (8UL << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk;
  pcnf0 |= (1UL << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk;
  pcnf0 |= (8UL << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk;
  pcnf0 |= (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) &
           RADIO_PCNF0_S1INCL_Msk;
  pcnf0 |= (RADIO_PCNF0_PLEN_16bit << RADIO_PCNF0_PLEN_Pos) &
           RADIO_PCNF0_PLEN_Msk;
  pcnf0 |= (RADIO_PCNF0_CRCINC_Exclude << RADIO_PCNF0_CRCINC_Pos) &
           RADIO_PCNF0_CRCINC_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_TERMLEN_Pos) & RADIO_PCNF0_TERMLEN_Msk;
  radio_->PCNF0 = pcnf0;

  uint32_t pcnf1 = 0U;
  pcnf1 |= (static_cast<uint32_t>(config_.maxPayloadLength)
            << RADIO_PCNF1_MAXLEN_Pos) &
           RADIO_PCNF1_MAXLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk;
  pcnf1 |= (3UL << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk;
  pcnf1 |= (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) &
           RADIO_PCNF1_ENDIAN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) &
           RADIO_PCNF1_WHITEEN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEOFFSET_Include << RADIO_PCNF1_WHITEOFFSET_Pos) &
           RADIO_PCNF1_WHITEOFFSET_Msk;
  radio_->PCNF1 = pcnf1;

  radio_->BASE0 = accessAddressBase(config_.accessAddress);
  radio_->PREFIX0 = (radio_->PREFIX0 & ~RADIO_PREFIX0_AP0_Msk) |
                    ((accessAddressPrefix(config_.accessAddress)
                      << RADIO_PREFIX0_AP0_Pos) &
                     RADIO_PREFIX0_AP0_Msk);
  radio_->TXADDRESS =
      (0UL << RADIO_TXADDRESS_TXADDRESS_Pos) & RADIO_TXADDRESS_TXADDRESS_Msk;
  radio_->RXADDRESSES =
      (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos) &
      RADIO_RXADDRESSES_ADDR0_Msk;

  uint32_t crccnf = 0U;
  crccnf |= (RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos) &
            RADIO_CRCCNF_LEN_Msk;
  crccnf |= (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos) &
            RADIO_CRCCNF_SKIPADDR_Msk;
  radio_->CRCCNF = crccnf;
  radio_->CRCPOLY = (kBleCrcPolynomial & RADIO_CRCPOLY_CRCPOLY_Msk);
  radio_->CRCINIT = (config_.crcInit & RADIO_CRCINIT_CRCINIT_Msk);

  radio_->TIFS = 150U;
  radio_->TXPOWER = ((txPowerRegFromDbm(config_.txPowerDbm)
                      << RADIO_TXPOWER_TXPOWER_Pos) &
                     RADIO_TXPOWER_TXPOWER_Msk);

  radio_->DFEPACKET.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dfePacket_)) &
      RADIO_DFEPACKET_PTR_PTR_Msk;
  radio_->DFEPACKET.MAXCNT =
      (sizeof(dfePacket_) << RADIO_DFEPACKET_MAXCNT_MAXCNT_Pos) &
      RADIO_DFEPACKET_MAXCNT_MAXCNT_Msk;
  for (uint8_t i = 0U; i < 2U; ++i) {
    radio_->AUXDATADMA[i].PTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&auxDataWords_[i * 2U])) &
        RADIO_AUXDATADMA_PTR_PTR_Msk;
    radio_->AUXDATADMA[i].MAXCNT =
        (2UL << RADIO_AUXDATADMA_MAXCNT_MAXCNT_Pos) &
        RADIO_AUXDATADMA_MAXCNT_MAXCNT_Msk;
    radio_->AUXDATADMA[i].ENABLE =
        (RADIO_AUXDATADMA_ENABLE_ENABLE_Disabled
         << RADIO_AUXDATADMA_ENABLE_ENABLE_Pos) &
        RADIO_AUXDATADMA_ENABLE_ENABLE_Msk;
    radio_->AUXDATA.CNF[i] = 0U;
  }
  configureRtt(false, false);

  return setLogicalChannel(config_.controlChannel);
}

bool BleChannelSoundingRadio::setLogicalChannel(uint8_t channelIndex) {
  if (radio_ == nullptr || !validLogicalChannel(channelIndex) ||
      !nrf54l15_exclusive_radio_is_owned_by(
          Nrf54ExclusiveRadioOwner::kRawChannelSounding,
          radioOwnershipToken_)) {
    return false;
  }

  const uint8_t freq = logicalChannelToFrequency(channelIndex);
  radio_->FREQUENCY =
      ((static_cast<uint32_t>(freq) << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);
  radio_->DATAWHITE = bleDataWhiteValue(channelIndex);
  return true;
}

void BleChannelSoundingRadio::configureRtt(bool enabled, bool reflectorRole) {
  if (!config_.enableRtt || !enabled) {
    radio_->RTT.CONFIG = 0U;
    return;
  }

  uint32_t config = 0U;
  config |= (RADIO_RTT_CONFIG_EN_Enabled << RADIO_RTT_CONFIG_EN_Pos) &
            RADIO_RTT_CONFIG_EN_Msk;
  config |= ((config_.rttFullAccessAddress ? RADIO_RTT_CONFIG_ENFULLAA_Enabled
                                           : RADIO_RTT_CONFIG_ENFULLAA_Disabled)
             << RADIO_RTT_CONFIG_ENFULLAA_Pos) &
            RADIO_RTT_CONFIG_ENFULLAA_Msk;
  config |= ((reflectorRole ? RADIO_RTT_CONFIG_ROLE_Reflector
                            : RADIO_RTT_CONFIG_ROLE_Initiator)
             << RADIO_RTT_CONFIG_ROLE_Pos) &
            RADIO_RTT_CONFIG_ROLE_Msk;
  config |= ((static_cast<uint32_t>(config_.rttNumSegments)
              << RADIO_RTT_CONFIG_NUMSEGMENTS_Pos) &
             RADIO_RTT_CONFIG_NUMSEGMENTS_Msk);
  config |= ((static_cast<uint32_t>(config_.rttEfsDelay)
              << RADIO_RTT_CONFIG_EFSDELAY_Pos) &
             RADIO_RTT_CONFIG_EFSDELAY_Msk);
  radio_->RTT.CONFIG = config;
  radio_->RTT.SEGMENT01 = 0U;
  radio_->RTT.SEGMENT23 = 0U;
  radio_->RTT.SEGMENT45 = 0U;
  radio_->RTT.SEGMENT67 = 0U;
}

void BleChannelSoundingRadio::prepareAuxDataCapture() {
  memset(auxDataWords_, 0, sizeof(auxDataWords_));
  radio_->EVENTS_AUXDATADMAEND = 0U;
  for (uint8_t i = 0U; i < 2U; ++i) {
    radio_->AUXDATA.CNF[i] =
        ((RADIO_AUXDATA_CNF_ACQMODE_Rtt << RADIO_AUXDATA_CNF_ACQMODE_Pos) &
         RADIO_AUXDATA_CNF_ACQMODE_Msk) |
        ((RADIO_AUXDATA_CNF_DIR_Acq << RADIO_AUXDATA_CNF_DIR_Pos) &
         RADIO_AUXDATA_CNF_DIR_Msk);
    radio_->AUXDATADMA[i].PTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&auxDataWords_[i * 2U])) &
        RADIO_AUXDATADMA_PTR_PTR_Msk;
    radio_->AUXDATADMA[i].MAXCNT =
        (2UL << RADIO_AUXDATADMA_MAXCNT_MAXCNT_Pos) &
        RADIO_AUXDATADMA_MAXCNT_MAXCNT_Msk;
    radio_->AUXDATADMA[i].ENABLE =
        (RADIO_AUXDATADMA_ENABLE_ENABLE_Enabled
         << RADIO_AUXDATADMA_ENABLE_ENABLE_Pos) &
        RADIO_AUXDATADMA_ENABLE_ENABLE_Msk;
  }
}

void BleChannelSoundingRadio::configureTxToneExtension() {
  radio_->DFEMODE = ((RADIO_DFEMODE_DFEOPMODE_AoD << RADIO_DFEMODE_DFEOPMODE_Pos) &
                     RADIO_DFEMODE_DFEOPMODE_Msk);

  uint32_t dfectrl1 = 0U;
  dfectrl1 |= (static_cast<uint32_t>(config_.cteTimeUnits)
               << RADIO_DFECTRL1_NUMBEROF8US_Pos) &
              RADIO_DFECTRL1_NUMBEROF8US_Msk;
  dfectrl1 |= (RADIO_DFECTRL1_DFEINEXTENSION_CRC
               << RADIO_DFECTRL1_DFEINEXTENSION_Pos) &
              RADIO_DFECTRL1_DFEINEXTENSION_Msk;
  dfectrl1 |= (RADIO_DFECTRL1_TSWITCHSPACING_2us
               << RADIO_DFECTRL1_TSWITCHSPACING_Pos) &
              RADIO_DFECTRL1_TSWITCHSPACING_Msk;
  dfectrl1 |= (RADIO_DFECTRL1_TSAMPLESPACINGREF_500ns
               << RADIO_DFECTRL1_TSAMPLESPACINGREF_Pos) &
              RADIO_DFECTRL1_TSAMPLESPACINGREF_Msk;
  dfectrl1 |= (RADIO_DFECTRL1_SAMPLETYPE_IQ
               << RADIO_DFECTRL1_SAMPLETYPE_Pos) &
              RADIO_DFECTRL1_SAMPLETYPE_Msk;
  dfectrl1 |= (RADIO_DFECTRL1_TSAMPLESPACING_500ns
               << RADIO_DFECTRL1_TSAMPLESPACING_Pos) &
              RADIO_DFECTRL1_TSAMPLESPACING_Msk;
  if (config_.dfeSwitchPatternCount > 0U) {
    dfectrl1 |= ((static_cast<uint32_t>(config_.dfeRepeatPattern)
                  << RADIO_DFECTRL1_REPEATPATTERN_Pos) &
                 RADIO_DFECTRL1_REPEATPATTERN_Msk);
  }
  radio_->DFECTRL1 = dfectrl1;
  radio_->DFECTRL2 =
      encodeSignedField(config_.dfeSwitchOffset16M,
                        13U,
                        RADIO_DFECTRL2_TSWITCHOFFSET_Msk,
                        RADIO_DFECTRL2_TSWITCHOFFSET_Pos) |
      encodeSignedField(config_.dfeSampleOffset16M,
                        12U,
                        RADIO_DFECTRL2_TSAMPLEOFFSET_Msk,
                        RADIO_DFECTRL2_TSAMPLEOFFSET_Pos);

  radio_->CLEARPATTERN = 1U;
  for (uint8_t i = 0U; i < config_.dfeSwitchPatternCount; ++i) {
    radio_->SWITCHPATTERN =
        (static_cast<uint32_t>(config_.dfeSwitchPattern[i])
         << RADIO_SWITCHPATTERN_SWITCHPATTERN_Pos) &
        RADIO_SWITCHPATTERN_SWITCHPATTERN_Msk;
  }

  const uint32_t numSamples = static_cast<uint32_t>(config_.cteTimeUnits) * 16UL;
  const uint32_t coeff =
      (65536UL + (static_cast<uint32_t>(config_.cteTimeUnits) / 2UL)) /
      static_cast<uint32_t>(config_.cteTimeUnits);
  radio_->CSTONES.MODE = ((RADIO_CSTONES_MODE_TPM_Enabled
                           << RADIO_CSTONES_MODE_TPM_Pos) &
                          RADIO_CSTONES_MODE_TPM_Msk) |
                         ((RADIO_CSTONES_MODE_TFM_Disabled
                           << RADIO_CSTONES_MODE_TFM_Pos) &
                          RADIO_CSTONES_MODE_TFM_Msk);
  radio_->CSTONES.NUMSAMPLES =
      (numSamples << RADIO_CSTONES_NUMSAMPLES_NUMSAMPLES_Pos) &
      RADIO_CSTONES_NUMSAMPLES_NUMSAMPLES_Msk;
  radio_->CSTONES.NEXTFREQUENCY = 0U;
  radio_->CSTONES.FAEPEER = 0U;
  radio_->CSTONES.PHASESHIFT = 0U;
  radio_->CSTONES.NUMSAMPLESCOEFF =
      (coeff << RADIO_CSTONES_NUMSAMPLESCOEFF_NUMSAMPLESCOEFF_Pos) &
      RADIO_CSTONES_NUMSAMPLESCOEFF_NUMSAMPLESCOEFF_Msk;
  radio_->CSTONES.DOWNSAMPLE =
      ((RADIO_CSTONES_DOWNSAMPLE_ENABLEFILTER_OFF
        << RADIO_CSTONES_DOWNSAMPLE_ENABLEFILTER_Pos) &
       RADIO_CSTONES_DOWNSAMPLE_ENABLEFILTER_Msk) |
      ((RADIO_CSTONES_DOWNSAMPLE_RATE_BLE2M
        << RADIO_CSTONES_DOWNSAMPLE_RATE_Pos) &
       RADIO_CSTONES_DOWNSAMPLE_RATE_Msk);
}

void BleChannelSoundingRadio::configureRxToneCapture() {
  configureTxToneExtension();
  radio_->DFEMODE = ((RADIO_DFEMODE_DFEOPMODE_AoA << RADIO_DFEMODE_DFEOPMODE_Pos) &
                     RADIO_DFEMODE_DFEOPMODE_Msk);

  uint32_t cteInline = 0U;
  cteInline |= (RADIO_CTEINLINECONF_CTEINLINECTRLEN_Enabled
                << RADIO_CTEINLINECONF_CTEINLINECTRLEN_Pos) &
               RADIO_CTEINLINECONF_CTEINLINECTRLEN_Msk;
  cteInline |= (RADIO_CTEINLINECONF_CTEINFOINS1_InS1
                << RADIO_CTEINLINECONF_CTEINFOINS1_Pos) &
               RADIO_CTEINLINECONF_CTEINFOINS1_Msk;
  cteInline |= (RADIO_CTEINLINECONF_CTEERRORHANDLING_No
                << RADIO_CTEINLINECONF_CTEERRORHANDLING_Pos) &
               RADIO_CTEINLINECONF_CTEERRORHANDLING_Msk;
  cteInline |= (RADIO_CTEINLINECONF_CTETIMEVALIDRANGE_20
                << RADIO_CTEINLINECONF_CTETIMEVALIDRANGE_Pos) &
               RADIO_CTEINLINECONF_CTETIMEVALIDRANGE_Msk;
  cteInline |= (RADIO_CTEINLINECONF_CTEINLINERXMODE1US_1us
                << RADIO_CTEINLINECONF_CTEINLINERXMODE1US_Pos) &
               RADIO_CTEINLINECONF_CTEINLINERXMODE1US_Msk;
  cteInline |= (RADIO_CTEINLINECONF_CTEINLINERXMODE2US_1us
                << RADIO_CTEINLINECONF_CTEINLINERXMODE2US_Pos) &
               RADIO_CTEINLINECONF_CTEINLINERXMODE2US_Msk;
  cteInline |= (static_cast<uint32_t>(config_.s0Pattern)
                << RADIO_CTEINLINECONF_S0CONF_Pos) &
               RADIO_CTEINLINECONF_S0CONF_Msk;
  cteInline |= (0xFFUL << RADIO_CTEINLINECONF_S0MASK_Pos) &
               RADIO_CTEINLINECONF_S0MASK_Msk;
  radio_->CTEINLINECONF = cteInline;
}

void BleChannelSoundingRadio::clearEvents() { clearRadioEvents(radio_); }

uint8_t BleChannelSoundingRadio::makeCteInfo() const {
  return static_cast<uint8_t>((kCteTypeAoA << 6U) |
                              (config_.cteTimeUnits & 0x1FU));
}

bool BleChannelSoundingRadio::sendFrame(uint8_t logicalChannel,
                                        PacketType type,
                                        uint8_t sequence,
                                        uint8_t channelIndex,
                                        uint8_t flags,
                                        const uint8_t* extra,
                                        uint8_t extraLen,
                                        bool enableRtt,
                                        bool rttReflectorRole) {
  if (!initialized_ || !validLogicalChannel(logicalChannel) ||
      !validDataChannel(channelIndex)) {
    return false;
  }

  const uint8_t payloadLen =
      static_cast<uint8_t>(kPayloadHeaderLen + extraLen);
  if (payloadLen == 0U || payloadLen > config_.maxPayloadLength) {
    return false;
  }
  if (extraLen > 0U && extra == nullptr) {
    return false;
  }

  configureTxToneExtension();
  configureRtt(enableRtt, rttReflectorRole);
  if (!setLogicalChannel(logicalChannel)) {
    return false;
  }

  txPacket_[0] = config_.s0Pattern;
  txPacket_[1] = payloadLen;
  txPacket_[2] = makeCteInfo();
  txPacket_[3] = kMagic0;
  txPacket_[4] = kMagic1;
  txPacket_[5] = static_cast<uint8_t>(type);
  txPacket_[6] = sequence;
  txPacket_[7] = channelIndex;
  txPacket_[8] = flags;
  if (extraLen > 0U) {
    memcpy(&txPacket_[9], extra, extraLen);
  }

  clearEvents();
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(txPacket_)) &
      RADIO_PACKETPTR_PTR_Msk;
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool endSeen = waitForRadioPhyEnd(radio_, kRadioEndBudgetUs);
  const bool disabled = waitForRadioDisabled(radio_, kRadioDisableBudgetUs);
  radio_->SHORTS = 0U;
  if (disabled) {
    clearEvents();
  } else {
    initialized_ = false;
  }
  return endSeen && disabled;
}

bool BleChannelSoundingRadio::decodeFrame(const uint8_t* packet,
                                          size_t packetLen,
                                          int8_t rssiDbm,
                                          RxFrame* outFrame) const {
  if (packet == nullptr || outFrame == nullptr || packetLen < 3U + kPayloadHeaderLen) {
    return false;
  }
  if (packet[0] != config_.s0Pattern) {
    return false;
  }

  const uint8_t payloadLen = packet[1];
  if (payloadLen < kPayloadHeaderLen ||
      payloadLen > config_.maxPayloadLength ||
      packetLen < static_cast<size_t>(3U + payloadLen)) {
    return false;
  }

  const uint8_t* payload = &packet[3];
  if (payload[0] != kMagic0 || payload[1] != kMagic1) {
    return false;
  }

  const uint8_t typeValue = payload[2];
  if (typeValue != static_cast<uint8_t>(PacketType::kControl) &&
      typeValue != static_cast<uint8_t>(PacketType::kProbe) &&
      typeValue != static_cast<uint8_t>(PacketType::kReport) &&
      typeValue != static_cast<uint8_t>(PacketType::kStepData) &&
      typeValue != static_cast<uint8_t>(PacketType::kStepAck)) {
    return false;
  }

  outFrame->valid = true;
  outFrame->type = static_cast<PacketType>(typeValue);
  outFrame->sequence = payload[3];
  outFrame->channelIndex = payload[4];
  outFrame->flags = payload[5];
  outFrame->rssiDbm = rssiDbm;
  outFrame->extraLen = static_cast<uint8_t>(payloadLen - kPayloadHeaderLen);
  if (outFrame->extraLen > sizeof(outFrame->extra)) {
    return false;
  }
  if (outFrame->extraLen > 0U) {
    memcpy(outFrame->extra, &payload[kPayloadHeaderLen], outFrame->extraLen);
  }
  return validDataChannel(outFrame->channelIndex);
}

void BleChannelSoundingRadio::encodeReportExtra(const BleCsToneSample& tone,
                                                uint8_t* outExtra) const {
  if (outExtra == nullptr) {
    return;
  }

  writeLe16(&outExtra[0], static_cast<uint16_t>(tone.i));
  writeLe16(&outExtra[2], static_cast<uint16_t>(tone.q));
  writeLe16(&outExtra[4], tone.magnitude);
  writeLe16(&outExtra[6], tone.magnitudeStd);
  outExtra[8] = tone.cteTimeUnits;
  outExtra[9] = tone.cteType;
  outExtra[10] = static_cast<uint8_t>(tone.rssiDbm);
}

void BleChannelSoundingRadio::decodeReportExtra(const uint8_t* extra,
                                                uint8_t extraLen,
                                                BleCsToneSample* outTone) const {
  if (extra == nullptr || outTone == nullptr || extraLen < kReportToneExtraLen) {
    return;
  }

  outTone->i = static_cast<int16_t>(readLe16(&extra[0]));
  outTone->q = static_cast<int16_t>(readLe16(&extra[2]));
  outTone->magnitude = readLe16(&extra[4]);
  outTone->magnitudeStd = readLe16(&extra[6]);
  outTone->cteTimeUnits = extra[8];
  outTone->cteType = extra[9];
  outTone->rssiDbm = static_cast<int8_t>(extra[10]);
  outTone->valid = (outTone->magnitude >= config_.minToneMagnitude) &&
                   ((outTone->i != 0) || (outTone->q != 0));
}

void BleChannelSoundingRadio::encodeRttExtra(const BleCsRttSample& rtt,
                                             uint8_t* outExtra) const {
  if (outExtra == nullptr) {
    return;
  }

  outExtra[0] = rtt.rawLen;
  memset(&outExtra[1], 0, kReportRttExtraLen - 1U);
  const uint8_t copyLen =
      (rtt.rawLen <= sizeof(rtt.rawBytes)) ? rtt.rawLen : sizeof(rtt.rawBytes);
  if (copyLen > 0U) {
    memcpy(&outExtra[1], rtt.rawBytes, copyLen);
  }
}

void BleChannelSoundingRadio::parseRttRaw(BleCsRttSample* outRtt) const {
  if (outRtt == nullptr || outRtt->rawLen == 0U) {
    return;
  }

  outRtt->present = true;
  outRtt->valid = false;

  // Keep raw AUXDATA bytes for debug, but do not derive RTT timing fields
  // from an inferred layout that does not match the observed hardware output.
  if (rawBytesAllZero(outRtt->rawBytes, outRtt->rawLen)) {
    outRtt->present = false;
  }
}

void BleChannelSoundingRadio::decodeRttExtra(const uint8_t* extra,
                                             uint8_t extraLen,
                                             BleCsRttSample* outRtt) const {
  if (extra == nullptr || outRtt == nullptr || extraLen < kReportRttExtraLen) {
    return;
  }

  outRtt->rawLen = extra[0];
  if (outRtt->rawLen > sizeof(outRtt->rawBytes)) {
    outRtt->rawLen = sizeof(outRtt->rawBytes);
  }
  if (outRtt->rawLen > (extraLen - 1U)) {
    outRtt->rawLen = static_cast<uint8_t>(extraLen - 1U);
  }
  memset(outRtt->rawBytes, 0, sizeof(outRtt->rawBytes));
  if (outRtt->rawLen > 0U) {
    memcpy(outRtt->rawBytes, &extra[1], outRtt->rawLen);
  }
  parseRttRaw(outRtt);
}

void BleChannelSoundingRadio::captureAuxDataRtt(BleCsRttSample* outRtt) {
  if (outRtt == nullptr) {
    return;
  }

  outRtt->present = false;
  outRtt->valid = false;
  outRtt->rawLen = 0U;
  memset(outRtt->rawBytes, 0, sizeof(outRtt->rawBytes));

  BleCsRttSample candidates[2] = {};
  for (uint8_t i = 0U; i < 2U; ++i) {
    const uint32_t amountWords =
        (radio_->AUXDATADMA[i].AMOUNT & RADIO_AUXDATADMA_AMOUNT_AMOUNT_Msk) >>
        RADIO_AUXDATADMA_AMOUNT_AMOUNT_Pos;
    const uint8_t byteCount = static_cast<uint8_t>(
        ((amountWords * sizeof(uint32_t)) <= sizeof(candidates[i].rawBytes))
            ? (amountWords * sizeof(uint32_t))
            : sizeof(candidates[i].rawBytes));
    if (byteCount == 0U) {
      continue;
    }

    candidates[i].present = true;
    candidates[i].rawLen = byteCount;
    memcpy(candidates[i].rawBytes, &auxDataWords_[i * 2U], byteCount);
    parseRttRaw(&candidates[i]);
  }

  uint8_t selected = 0U;
  if (candidates[1].present) {
    bool firstAllZero = true;
    bool secondAllZero = true;
    for (uint8_t i = 0U; i < candidates[0].rawLen; ++i) {
      if (candidates[0].rawBytes[i] != 0U) {
        firstAllZero = false;
        break;
      }
    }
    for (uint8_t i = 0U; i < candidates[1].rawLen; ++i) {
      if (candidates[1].rawBytes[i] != 0U) {
        secondAllZero = false;
        break;
      }
    }

    if ((!candidates[0].present && candidates[1].present) ||
        (firstAllZero && !secondAllZero) ||
        (!candidates[0].valid && candidates[1].valid)) {
      selected = 1U;
    }
  }

  if (!candidates[selected].present) {
    return;
  }

  *outRtt = candidates[selected];
}

void BleChannelSoundingRadio::resetDfeCaptureState() {
  lastDfePacketAmountBytes_ = 0U;
  lastDfePacketCurrentAmountBytes_ = 0U;
  lastDfePacketAllZero_ = true;
}

void BleChannelSoundingRadio::updateDfeCaptureState() {
  const uint16_t amount = static_cast<uint16_t>(
      (radio_->DFEPACKET.AMOUNT & RADIO_DFEPACKET_AMOUNT_AMOUNT_Msk) >>
      RADIO_DFEPACKET_AMOUNT_AMOUNT_Pos);
  const uint16_t currentAmount = static_cast<uint16_t>(
      (radio_->DFEPACKET.CURRENTAMOUNT & RADIO_DFEPACKET_CURRENTAMOUNT_AMOUNT_Msk) >>
      RADIO_DFEPACKET_CURRENTAMOUNT_AMOUNT_Pos);
  const uint8_t cappedAmount = static_cast<uint8_t>(
      (amount <= sizeof(dfePacket_)) ? amount : sizeof(dfePacket_));
  lastDfePacketAmountBytes_ = amount;
  lastDfePacketCurrentAmountBytes_ = currentAmount;
  lastDfePacketAllZero_ = rawBytesAllZero(dfePacket_, cappedAmount);
}

bool BleChannelSoundingRadio::deriveToneFromRawDfe(BleCsToneSample* outTone) const {
  if (outTone == nullptr || lastDfePacketAmountBytes_ < 3U ||
      lastDfePacketAllZero_) {
    return false;
  }

  const size_t available =
      (lastDfePacketAmountBytes_ <= sizeof(dfePacket_))
          ? lastDfePacketAmountBytes_
          : sizeof(dfePacket_);
  const size_t sampleCount = available / 3U;
  if (sampleCount == 0U) {
    return false;
  }

  int32_t sumI = 0;
  int32_t sumQ = 0;
  uint16_t used = 0U;
  for (size_t i = 0U; i < sampleCount; ++i) {
    const BleCsIqSample sample = parsePctSample(&dfePacket_[i * 3U]);
    if (sample.i == 0 && sample.q == 0) {
      continue;
    }
    sumI += sample.i;
    sumQ += sample.q;
    ++used;
  }

  if (used == 0U) {
    return false;
  }

  const int16_t meanI = static_cast<int16_t>(sumI / static_cast<int32_t>(used));
  const int16_t meanQ = static_cast<int16_t>(sumQ / static_cast<int32_t>(used));
  const float mag =
      sqrtf(static_cast<float>(meanI) * static_cast<float>(meanI) +
            static_cast<float>(meanQ) * static_cast<float>(meanQ));
  const uint16_t magnitude =
      (mag >= 65535.0f) ? 65535U : static_cast<uint16_t>(mag + 0.5f);

  outTone->i = meanI;
  outTone->q = meanQ;
  outTone->magnitude = magnitude;
  outTone->magnitudeStd = 0U;
  outTone->cteTimeUnits = config_.cteTimeUnits;
  outTone->cteType = kCteTypeAoA;
  outTone->valid = (magnitude >= config_.minToneMagnitude) &&
                   ((meanI != 0) || (meanQ != 0));
  return outTone->valid;
}

bool BleChannelSoundingRadio::receiveFrame(uint8_t logicalChannel,
                                           uint32_t listenWindowUs,
                                           bool captureTone,
                                           bool captureRtt,
                                           bool rttReflectorRole,
                                           RxFrame* outFrame,
                                           BleCsToneSample* outTone,
                                           BleCsRttSample* outRtt) {
  if (!initialized_ || !validLogicalChannel(logicalChannel) || outFrame == nullptr) {
    return false;
  }

  *outFrame = RxFrame{};
  if (outTone != nullptr) {
    *outTone = BleCsToneSample{};
  }
  if (outRtt != nullptr) {
    *outRtt = BleCsRttSample{};
  }
  resetDfeCaptureState();

  configureRxToneCapture();
  configureRtt(captureRtt, rttReflectorRole);
  if (!setLogicalChannel(logicalChannel)) {
    return false;
  }

  memset(rxPacket_, 0, sizeof(rxPacket_));
  clearEvents();
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rxPacket_)) &
      RADIO_PACKETPTR_PTR_Msk;

  uint32_t shorts =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);
  if (!captureTone) {
    shorts |= ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
                << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
               RADIO_SHORTS_PHYEND_DISABLE_Msk);
  }
  radio_->SHORTS = shorts;
  if (captureRtt && outRtt != nullptr) {
    prepareAuxDataCapture();
    radio_->TASKS_AUXDATADMASTART =
        RADIO_TASKS_AUXDATADMASTART_TASKS_AUXDATADMASTART_Trigger;
  } else {
    for (uint8_t i = 0U; i < 2U; ++i) {
      radio_->AUXDATADMA[i].ENABLE =
          (RADIO_AUXDATADMA_ENABLE_ENABLE_Disabled
           << RADIO_AUXDATADMA_ENABLE_ENABLE_Pos) &
          RADIO_AUXDATADMA_ENABLE_ENABLE_Msk;
      radio_->AUXDATA.CNF[i] = 0U;
    }
  }
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;

  if (!waitForCrcDone(radio_, listenWindowUs)) {
    bool auxStopped = true;
    if (captureRtt) {
      auxStopped = stopAndDisableAuxDataDma(radio_, kAuxDataBudgetUs);
    }
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    const bool disabled =
        waitForRadioDisabled(radio_, kRadioDisableBudgetUs);
    radio_->SHORTS = 0U;
    if (!disabled || !auxStopped) {
      initialized_ = false;
      return false;
    }
    configureRtt(false, false);
    clearEvents();
    return false;
  }

  const bool crcOk = (radio_->EVENTS_CRCOK != 0U) ||
                     (((radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
                       RADIO_CRCSTATUS_CRCSTATUS_Pos) ==
                      RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  const int8_t rssiDbm = radioRssiDbm(radio_);

  if (!captureTone) {
    if (!waitForRadioDisabled(radio_, kRadioDisableBudgetUs)) {
      radio_->SHORTS = 0U;
      initialized_ = false;
      return false;
    }
  } else {
    if (!waitForRadioPhyEnd(radio_, kRadioEndBudgetUs)) {
      radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
      if (!waitForRadioDisabled(radio_, kRadioDisableBudgetUs)) {
        radio_->SHORTS = 0U;
        initialized_ = false;
        return false;
      }
      radio_->SHORTS = 0U;
      configureRtt(false, false);
      clearEvents();
      return false;
    }
    if (config_.enableRawDfeCapture) {
      updateDfeCaptureState();
    }
    if (crcOk && outTone != nullptr && (radio_->EVENTS_CTEPRESENT != 0U)) {
      radio_->EVENTS_CSTONESEND = 0U;
      radio_->TASKS_CSTONESSTART = RADIO_TASKS_CSTONESSTART_TASKS_CSTONESSTART_Trigger;
      const bool cstReady = waitForFlag(&radio_->EVENTS_CSTONESEND, kCstStartBudgetUs);
      if (cstReady) {
        const uint32_t pct16 = radio_->CSTONES.PCT16;
        const uint32_t magPhase = radio_->CSTONES.MAGPHASEMEAN;
        outTone->i = static_cast<int16_t>(pct16 & 0xFFFFU);
        outTone->q = static_cast<int16_t>((pct16 >> 16U) & 0xFFFFU);
        outTone->magnitude =
            static_cast<uint16_t>((magPhase >> 16U) & 0xFFFFU);
        outTone->phase = static_cast<uint16_t>(magPhase & 0xFFFFU);
        outTone->magnitudeStd = static_cast<uint16_t>(
            (radio_->CSTONES.MAGSTD & RADIO_CSTONES_MAGSTD_MAGSTD_Msk) >>
            RADIO_CSTONES_MAGSTD_MAGSTD_Pos);
        outTone->rssiDbm = rssiDbm;
        outTone->cteTimeUnits = static_cast<uint8_t>(
            (radio_->CTESTATUS & RADIO_CTESTATUS_CTETIME_Msk) >>
            RADIO_CTESTATUS_CTETIME_Pos);
        outTone->cteType = static_cast<uint8_t>(
            (radio_->CTESTATUS & RADIO_CTESTATUS_CTETYPE_Msk) >>
            RADIO_CTESTATUS_CTETYPE_Pos);
        outTone->valid = crcOk &&
                         (outTone->magnitude >= config_.minToneMagnitude) &&
                         ((outTone->i != 0) || (outTone->q != 0));
      }
    }
    if (crcOk && outTone != nullptr && !outTone->valid) {
      (void)deriveToneFromRawDfe(outTone);
      if (outTone->valid) {
        outTone->rssiDbm = rssiDbm;
      }
    }

    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    if (!waitForRadioDisabled(radio_, kRadioDisableBudgetUs)) {
      radio_->SHORTS = 0U;
      initialized_ = false;
      return false;
    }
  }

  if (captureRtt) {
    (void)waitForFlag(&radio_->EVENTS_AUXDATADMAEND, kAuxDataBudgetUs);
    if (!stopAndDisableAuxDataDma(radio_, kAuxDataBudgetUs)) {
      initialized_ = false;
      return false;
    }
    if (outRtt != nullptr) {
      captureAuxDataRtt(outRtt);
    }
  }

  radio_->SHORTS = 0U;
  configureRtt(false, false);

  bool decoded = false;
  if (crcOk) {
    const size_t frameLen = static_cast<size_t>(3U + rxPacket_[1]);
    decoded = decodeFrame(rxPacket_, frameLen, rssiDbm, outFrame);
  }

  if (captureTone) {
    clearRadioEventsPreserveCstones(radio_);
  } else {
    clearEvents();
  }
  return crcOk && decoded;
}

bool BleChannelSoundingRadio::measureChannel(uint8_t channelIndex,
                                             uint8_t sequence,
                                             BleCsChannelMeasurement* outMeasurement) {
  if (outMeasurement == nullptr) {
    return false;
  }

  *outMeasurement = BleCsChannelMeasurement{};
  outMeasurement->channelIndex = channelIndex;
  outMeasurement->sequence = sequence;
  if (!initialized_ || !validDataChannel(channelIndex)) {
    outMeasurement->status = 1U;
    return false;
  }

  for (uint8_t attempt = 0U; attempt < config_.probeRetries; ++attempt) {
    const uint32_t attemptStartUs = micros();
    if (!sendFrame(config_.controlChannel, PacketType::kControl, sequence,
                   channelIndex, 0U, nullptr, 0U, false, false)) {
      outMeasurement->status = 2U;
      continue;
    }
    outMeasurement->controlTxUs =
        static_cast<uint32_t>(micros() - attemptStartUs);

    waitElapsedMicros(config_.controlToProbeDelayUs);
    outMeasurement->controlToProbeGapUs =
        static_cast<uint32_t>(micros() - attemptStartUs);

    if (!sendFrame(channelIndex, PacketType::kProbe, sequence, channelIndex,
                   0U, nullptr, 0U, config_.enableRtt, false)) {
      outMeasurement->status = 3U;
      continue;
    }
    outMeasurement->probeTxUs =
        static_cast<uint32_t>(micros() - attemptStartUs);

    RxFrame report{};
    BleCsToneSample localTone{};
    BleCsRttSample localRtt{};
    if (!receiveFrame(channelIndex, config_.responseListenWindowUs, true,
                      config_.enableRtt, false, &report, &localTone, &localRtt)) {
      outMeasurement->reportRxUs =
          static_cast<uint32_t>(micros() - attemptStartUs);
      outMeasurement->status = 4U;
      continue;
    }
    outMeasurement->reportRxUs =
        static_cast<uint32_t>(micros() - attemptStartUs);
    if (!report.valid || report.type != PacketType::kReport ||
        report.sequence != sequence || report.channelIndex != channelIndex) {
      outMeasurement->status = 5U;
      continue;
    }

    BleCsToneSample peerTone{};
    BleCsRttSample peerRtt{};
    if ((report.flags & 0x01U) != 0U) {
      decodeReportExtra(report.extra, report.extraLen, &peerTone);
    }
    if ((report.flags & 0x02U) != 0U && report.extraLen >= kReportExtraLen) {
      decodeRttExtra(&report.extra[kReportToneExtraLen],
                     static_cast<uint8_t>(report.extraLen - kReportToneExtraLen),
                     &peerRtt);
    }

    outMeasurement->localTone = localTone;
    outMeasurement->peerTone = peerTone;
    outMeasurement->localRtt = localRtt;
    outMeasurement->peerRtt = peerRtt;
    if (localTone.valid && peerTone.valid) {
      outMeasurement->combinedPhaseRad = combinedPhaseRad(*outMeasurement);
      outMeasurement->combinedPhaseValid = true;
      outMeasurement->phaseSampleCount = 1U;
      outMeasurement->phaseCoherence = 1.0f;
    }
    (void)rttDistanceMeters(*outMeasurement, &outMeasurement->rttDistanceMeters);
    outMeasurement->valid =
        (localTone.valid && peerTone.valid) || (localRtt.valid && peerRtt.valid);
    outMeasurement->status = outMeasurement->valid ? 0U : 6U;
    return outMeasurement->valid;
  }

  return false;
}

bool BleChannelSoundingRadio::measureChannelAveraged(
    uint8_t channelIndex,
    uint8_t exchangeCount,
    uint8_t* inOutSequence,
    BleCsChannelMeasurement* outMeasurement,
    float minPhaseCoherence,
    uint16_t interExchangeGuardUs) {
  if (outMeasurement == nullptr) {
    return false;
  }

  *outMeasurement = BleCsChannelMeasurement{};
  outMeasurement->channelIndex = channelIndex;
  if (!initialized_ || !validDataChannel(channelIndex) ||
      inOutSequence == nullptr || exchangeCount < 3U ||
      !isfinite(minPhaseCoherence) || minPhaseCoherence < 0.0f ||
      minPhaseCoherence > 1.0f) {
    outMeasurement->status = 1U;
    return false;
  }

  BleCsChannelMeasurement representative{};
  float representativeQuality = 0.0f;
  float phaseSumI = 0.0f;
  float phaseSumQ = 0.0f;
  uint8_t validPhaseSamples = 0U;
  for (uint8_t exchange = 0U; exchange < exchangeCount; ++exchange) {
    const uint8_t sequence = *inOutSequence;
    *inOutSequence = static_cast<uint8_t>(sequence + 1U);

    BleCsChannelMeasurement measurement{};
    const bool measured = measureChannel(channelIndex, sequence, &measurement);
    if (measured && measurement.valid && measurement.combinedPhaseValid &&
        measurement.localTone.valid && measurement.peerTone.valid &&
        isfinite(measurement.combinedPhaseRad)) {
      const float quality = toneQualityScore(measurement.localTone,
                                             measurement.peerTone);
      if (validPhaseSamples == 0U || quality > representativeQuality) {
        representative = measurement;
        representativeQuality = quality;
      }
      phaseSumI += cosf(measurement.combinedPhaseRad);
      phaseSumQ += sinf(measurement.combinedPhaseRad);
      ++validPhaseSamples;
    }

    if ((exchange + 1U) < exchangeCount && interExchangeGuardUs > 0U) {
      waitElapsedMicros(interExchangeGuardUs);
    }
  }

  if (validPhaseSamples > 0U) {
    *outMeasurement = representative;
  }
  outMeasurement->channelIndex = channelIndex;
  outMeasurement->phaseSampleCount = validPhaseSamples;
  if (validPhaseSamples > 0U) {
    const float resultant = hypotf(phaseSumI, phaseSumQ);
    outMeasurement->phaseCoherence = fminf(
        1.0f, resultant / static_cast<float>(validPhaseSamples));
  }

  if (validPhaseSamples < 3U ||
      !isfinite(outMeasurement->phaseCoherence) ||
      outMeasurement->phaseCoherence < minPhaseCoherence) {
    outMeasurement->valid = false;
    outMeasurement->combinedPhaseValid = false;
    outMeasurement->status = 7U;
    return false;
  }

  outMeasurement->combinedPhaseRad = atan2f(phaseSumQ, phaseSumI);
  outMeasurement->combinedPhaseValid = true;
  outMeasurement->valid = true;
  outMeasurement->status = 0U;
  return true;
}

uint8_t BleChannelSoundingRadio::centeredDataChannelAt(uint8_t order,
                                                       uint8_t channelCount) {
  if (channelCount == 0U || channelCount > kMaxCsChannels || order >= channelCount) {
    return 0xFFU;
  }

  const uint8_t center = 18U;
  if (order == 0U) {
    return center;
  }

  const uint8_t step = static_cast<uint8_t>((order + 1U) / 2U);
  const uint8_t channel = ((order & 0x1U) != 0U)
                              ? static_cast<uint8_t>(center - step)
                              : static_cast<uint8_t>(center + step);
  return validDataChannel(channel) ? channel : 0xFFU;
}

bool BleChannelSoundingRadio::planConnectedWindow(
    const BleConnectionTimingSnapshot& snapshot,
    uint32_t requestedWindowUs,
    uint32_t guardBeforeUs,
    uint32_t guardAfterUs,
    BleCsConnectedWindowPlan* outPlan) {
  if (outPlan == nullptr) {
    return false;
  }

  BleCsConnectedWindowPlan plan{};
  plan.connected = snapshot.connected;
  plan.role = snapshot.role;
  plan.nextEventCounter = snapshot.nextEventCounter;
  plan.intervalUnits = snapshot.intervalUnits;
  plan.intervalUs = snapshot.intervalUs;
  plan.nowUs = snapshot.nowUs;
  plan.nextEventUs = snapshot.nextEventUs;
  plan.timeUntilNextEventUs = snapshot.timeUntilNextEventUs;
  plan.requestedWindowUs = requestedWindowUs;
  plan.guardBeforeUs = guardBeforeUs;
  plan.guardAfterUs = guardAfterUs;

  if (!snapshot.connected || snapshot.role == BleConnectionRole::kNone) {
    plan.reason = 1U;
    *outPlan = plan;
    return false;
  }
  if (requestedWindowUs == 0U || snapshot.intervalUs == 0U) {
    plan.reason = 2U;
    *outPlan = plan;
    return false;
  }
  const uint32_t guardTotalUs = guardBeforeUs + guardAfterUs;
  if (guardTotalUs < guardBeforeUs) {
    plan.reason = 5U;
    *outPlan = plan;
    return false;
  }
  if (snapshot.timeUntilNextEventUs <= guardTotalUs) {
    plan.valid = true;
    plan.startUs = static_cast<uint32_t>(snapshot.nowUs + guardBeforeUs);
    plan.deadlineUs =
        (snapshot.nextEventUs > guardAfterUs)
            ? static_cast<uint32_t>(snapshot.nextEventUs - guardAfterUs)
            : snapshot.nextEventUs;
    plan.reason = 3U;
    *outPlan = plan;
    return false;
  }

  plan.valid = true;
  plan.startUs = static_cast<uint32_t>(snapshot.nowUs + guardBeforeUs);
  plan.deadlineUs = static_cast<uint32_t>(snapshot.nextEventUs - guardAfterUs);
  plan.availableUs = static_cast<uint32_t>(snapshot.timeUntilNextEventUs -
                                           guardTotalUs);
  plan.fits = plan.availableUs >= requestedWindowUs;
  plan.reason = plan.fits ? 0U : 4U;
  *outPlan = plan;
  return plan.fits;
}

bool BleChannelSoundingRadio::measureConnectedWindowChannel(
    const BleConnectionTimingSnapshot& snapshot,
    uint8_t channelIndex,
    uint8_t sequence,
    uint32_t requestedWindowUs,
    uint32_t guardBeforeUs,
    uint32_t guardAfterUs,
    BleCsConnectedWindowMeasurement* outMeasurement) {
  if (outMeasurement == nullptr) {
    return false;
  }

  BleCsConnectedWindowMeasurement result{};
  result.channelIndex = channelIndex;
  result.sequence = sequence;

  BleCsConnectedWindowPlan plan{};
  const bool fits = planConnectedWindow(snapshot, requestedWindowUs,
                                        guardBeforeUs, guardAfterUs, &plan);
  result.plan = plan;
  if (!fits) {
    result.reason = plan.reason;
    *outMeasurement = result;
    return false;
  }
  if (!initialized_) {
    result.reason = 6U;
    *outMeasurement = result;
    return false;
  }
  if (!validDataChannel(channelIndex)) {
    result.reason = 7U;
    *outMeasurement = result;
    return false;
  }

  const uint32_t scheduleStartUs = micros();
  while (static_cast<uint32_t>(micros() - scheduleStartUs) < guardBeforeUs) {
    __asm volatile("nop");
  }

  const uint32_t startUs = micros();
  result.startDelayUs = static_cast<uint32_t>(startUs - scheduleStartUs);
  if (result.startDelayUs >= static_cast<uint32_t>(guardBeforeUs + plan.availableUs)) {
    result.reason = 8U;
    *outMeasurement = result;
    return false;
  }

  result.attempted = true;
  BleCsChannelMeasurement measurement{};
  const bool ok = measureChannel(channelIndex, sequence, &measurement);
  const uint32_t endUs = micros();
  result.elapsedUs = static_cast<uint32_t>(endUs - startUs);
  result.remainingUs =
      (result.elapsedUs < plan.availableUs)
          ? static_cast<uint32_t>(plan.availableUs - result.elapsedUs)
          : 0U;
  result.completedBeforeDeadline = result.elapsedUs <= plan.availableUs;
  result.measurement = measurement;
  result.measured = ok && measurement.valid;
  result.reason = result.measured ? (result.completedBeforeDeadline ? 0U : 10U)
                                  : 9U;
  *outMeasurement = result;
  return result.measured && result.completedBeforeDeadline;
}

struct BleCsTimedMode2ResultSearch {
  uint32_t token = 0U;
  uint8_t channel = 0xFFU;
  bool peerSide = false;
  bool found = false;
};

static const uint8_t kBleCsTimedMode2LocalPctSample[3] = {0x00U, 0x04U, 0x00U};
static const uint8_t kBleCsTimedMode2PeerPctSamples[39][3] = {
    {0xF0U, 0xB3U, 0xF4U}, {0xE2U, 0xC3U, 0xF0U}, {0xD1U, 0xE3U, 0xECU},
    {0xBCU, 0x13U, 0xE9U}, {0xA3U, 0x63U, 0xE5U}, {0x86U, 0xC3U, 0xE1U},
    {0x66U, 0x43U, 0xDEU}, {0x43U, 0xF3U, 0xDAU}, {0x1CU, 0xB3U, 0xD7U},
    {0xF2U, 0xB2U, 0xD4U}, {0xC4U, 0xD2U, 0xD1U}, {0x62U, 0xA2U, 0xCCU},
    {0x2DU, 0x52U, 0xCAU}, {0xF6U, 0x41U, 0xC8U}, {0xBDU, 0x61U, 0xC6U},
    {0x82U, 0xC1U, 0xC4U}, {0x46U, 0x51U, 0xC3U}, {0x08U, 0x31U, 0xC2U},
    {0xCAU, 0x40U, 0xC1U}, {0x8AU, 0x90U, 0xC0U}, {0x4AU, 0x30U, 0xC0U},
    {0x0AU, 0x00U, 0xC0U}, {0xC9U, 0x1FU, 0xC0U}, {0x89U, 0x7FU, 0xC0U},
    {0x4AU, 0x0FU, 0xC1U}, {0x0BU, 0xEFU, 0xC1U}, {0xCDU, 0xFEU, 0xC2U},
    {0x90U, 0x4EU, 0xC4U}, {0x55U, 0xDEU, 0xC5U}, {0x1BU, 0xAEU, 0xC7U},
    {0xE3U, 0xADU, 0xC9U}, {0xAEU, 0xEDU, 0xCBU}, {0x7AU, 0x5DU, 0xCEU},
    {0x4AU, 0xFDU, 0xD0U}, {0x1CU, 0xCDU, 0xD3U}, {0xF1U, 0xCCU, 0xD6U},
    {0xC9U, 0xFCU, 0xD9U}, {0xA4U, 0x4CU, 0xDDU}, {0x83U, 0xBCU, 0xE0U},
};

static int16_t clampBleCsPct12Component(int32_t value) {
  if (value < -2048) {
    return -2048;
  }
  if (value > 2047) {
    return 2047;
  }
  return static_cast<int16_t>(value);
}

static BleCsIqSample scaleBleCsPctSample(const uint8_t pct[3], uint16_t scaleQ10) {
  const BleCsIqSample sample = BleChannelSoundingRadio::parsePctSample(pct);
  BleCsIqSample scaled{};
  scaled.i = clampBleCsPct12Component(
      ((static_cast<int32_t>(sample.i) * static_cast<int32_t>(scaleQ10)) + 512) /
      1024);
  scaled.q = clampBleCsPct12Component(
      ((static_cast<int32_t>(sample.q) * static_cast<int32_t>(scaleQ10)) + 512) /
      1024);
  return scaled;
}

static BleCsIqSample bleCsExpectedTimedMode2ResultSample(uint32_t token,
                                                        uint8_t channel,
                                                        bool peerSide) {
  const uint8_t* pct =
      (peerSide && channel < 39U) ? kBleCsTimedMode2PeerPctSamples[channel]
                                  : kBleCsTimedMode2LocalPctSample;
  const uint16_t scaleQ10 = static_cast<uint16_t>(
      960U + ((token ^ (token >> 16U) ^
               (static_cast<uint32_t>(channel) << 5U) ^
               (peerSide ? 0x155UL : 0x2AAUL)) &
              0x3FUL));
  return scaleBleCsPctSample(pct, scaleQ10);
}

static bool bleCsTimedMode2ResultSearchCallback(const BleCsSubeventStep* step,
                                                void* userData) {
  BleCsTimedMode2ResultSearch* context =
      static_cast<BleCsTimedMode2ResultSearch*>(userData);
  if (context == nullptr || step == nullptr) {
    return false;
  }
  if (context->found || step->mode != kBleCsMainMode2 ||
      step->channel != context->channel) {
    return true;
  }

  BleCsStepMode2Data mode2{};
  if (!BleChannelSoundingRadio::parseMode2StepData(step, &mode2) ||
      mode2.toneCount == 0U) {
    return true;
  }

  BleCsStepToneInfo tone{};
  if (!BleChannelSoundingRadio::parseMode2ToneInfo(step, 0U, &tone) ||
      tone.extensionIndicator != kBleCsToneExtensionNone) {
    return true;
  }

  const BleCsIqSample expected = bleCsExpectedTimedMode2ResultSample(
      context->token, context->channel, context->peerSide);
  context->found = tone.pct.i == expected.i && tone.pct.q == expected.q;
  return true;
}

static bool bleCsSubeventResultContainsTimedMode2Observation(
    const BleCsSubeventResult& result,
    uint32_t token,
    uint8_t channel,
    bool peerSide) {
  if (token == 0U || !validDataChannel(channel) ||
      result.stepData == nullptr || result.stepDataLen == 0U) {
    return false;
  }

  BleCsTimedMode2ResultSearch context{};
  context.token = token;
  context.channel = channel;
  context.peerSide = peerSide;
  BleChannelSoundingRadio::parseSubeventStepData(
      result.stepData, result.stepDataLen,
      bleCsTimedMode2ResultSearchCallback, &context);
  return context.found;
}

bool BleCsConnectedMode2SweepRunner::runInitiator(
    BleRadio& ble,
    BleChannelSoundingRadio& radio,
    BleCsControllerVprHost* host,
    const BleCsConnectedMode2SweepConfig& config,
    BleCsChannelMeasurement* measurements,
    uint8_t* localStepData,
    size_t localMaxStepDataLen,
    uint8_t* peerStepData,
    size_t peerMaxStepDataLen,
    BleCsConnectedMode2SweepResult* outResult,
    BleCsConnectedMode2ChannelCallback channelCallback,
    void* channelCallbackUserData) {
  BleCsConnectedMode2SweepResult result{};
  if (outResult == nullptr || config.channels == nullptr ||
      measurements == nullptr || config.channelCount == 0U ||
      config.channelCount > kMaxCsChannels) {
    if (outResult != nullptr) {
      *outResult = result;
    }
    return false;
  }

  uint8_t localSequence = 0U;
  uint8_t* sequence = (config.inOutSequence != nullptr)
                          ? config.inOutSequence
                          : &localSequence;
  const BleCsVprMeasurementWorkItem* work = config.workItem;
  const bool workItemApplied =
      work != nullptr && work->valid && work->status == 0U && work->ready &&
      work->procedureCounter != 0U && work->configId != 0U &&
      work->subeventStepCount != 0U && work->totalSteps != 0U;
  const uint8_t effectiveConfigId =
      workItemApplied ? work->configId : config.configId;
  uint8_t effectiveChannels[kMaxCsChannels] = {0};
  uint8_t effectiveChannelCount = 0U;
  if (workItemApplied) {
    result.workItemApplied = true;
    result.workConfigId = work->configId;
    result.workProcedureCounter = work->procedureCounter;
    result.workSubeventIndex = work->activeSubeventIndex;
    result.workSubeventCount = work->totalSubevents;
    result.workSubeventStepCount = work->subeventStepCount;
    result.workTotalSteps = work->totalSteps;
    result.workStepChannelCount = work->stepChannelCount;
    result.workAutoExecuted = work->controllerAutoExecuted;
    result.workAutoBlockMask = work->controllerAutoBlockMask;
    result.workAutoCount = work->controllerAutoCount;
    result.workAutoServiceCalls = work->controllerAutoServiceCalls;
    result.workAutoDuePasses = work->controllerAutoDuePasses;
    result.workAutoProcedureCounter = work->controllerAutoProcedureCounter;
    result.workAutoSubevent = work->controllerAutoSubevent;
    result.workAutoStatus = work->controllerAutoStatus;
    memcpy(result.workStepChannels, work->stepChannels,
           sizeof(result.workStepChannels));
    const uint8_t workChannelLimit =
        (work->stepChannelCount < config.channelCount)
            ? work->stepChannelCount
            : config.channelCount;
    for (uint8_t i = 0U; i < workChannelLimit; ++i) {
      if (validDataChannel(work->stepChannels[i])) {
        effectiveChannels[effectiveChannelCount++] = work->stepChannels[i];
      }
    }
    result.workChannelsUsed = effectiveChannelCount > 0U;
  }
  if (effectiveChannelCount == 0U) {
    for (uint8_t i = 0U; i < config.channelCount; ++i) {
      effectiveChannels[effectiveChannelCount++] = config.channels[i];
    }
  }
  result.sweepChannelCount = effectiveChannelCount;

  bool workExecutionOk = true;
  uint32_t workHostLocalResultPacketDelta = 0U;
  uint32_t workHostPeerResultPacketDelta = 0U;
  uint32_t workHostControllerEventPacketDelta = 0U;
  uint32_t workHostPeerResultMarkerDelta = 0U;
  BleCsControllerHostState workHostStateBefore{};
  bool workHostStateBaselineValid = false;
  const auto updateWorkHostResultDeltas = [&]() -> void {
    if (host == nullptr || !workHostStateBaselineValid) {
      return;
    }
    const BleCsControllerHostState workHostStateAfter = host->hostState();
    workHostLocalResultPacketDelta =
        workHostStateAfter.localResultPackets -
        workHostStateBefore.localResultPackets;
    workHostPeerResultPacketDelta =
        workHostStateAfter.peerResultPackets -
        workHostStateBefore.peerResultPackets;
    workHostControllerEventPacketDelta =
        workHostStateAfter.controllerEventPackets -
        workHostStateBefore.controllerEventPackets;
    workHostPeerResultMarkerDelta =
        workHostStateAfter.controllerPeerResultMarkers -
        workHostStateBefore.controllerPeerResultMarkers;
  };
  const auto captureWorkDrainStats = [&]() -> void {
    if (host == nullptr || !workHostStateBaselineValid) {
      return;
    }
    const BleCsControllerVprDrainStats& workDrainStats =
        host->lastDrainStats();
    const bool hasDrainActivity =
        workDrainStats.packetsRead != 0U ||
        workDrainStats.packetsConsumed != 0U ||
        workDrainStats.packetsRejected != 0U ||
        workDrainStats.readFailures != 0U ||
        workDrainStats.pendingPacketsPopped != 0U;
    if (hasDrainActivity) {
      result.workDirectDrainPackets = workDrainStats.packetsRead;
      result.workDirectDrainConsumed = workDrainStats.packetsConsumed;
      result.workDirectDrainRejected = workDrainStats.packetsRejected;
      result.workDirectDrainReadFailures = workDrainStats.readFailures;
      result.workDirectDrainLastLen = workDrainStats.lastPacketLen;
      result.workDirectDrainFirstRejectedLen =
          workDrainStats.firstRejectedPacketLen;
      result.workDirectDrainFirstRejectedConnHandle =
          workDrainStats.firstRejectedConnHandle;
      result.workDirectDrainFirstRejectedProcedureCounter =
          workDrainStats.firstRejectedProcedureCounter;
      result.workDirectDrainFirstRejectedResultLen =
          workDrainStats.firstRejectedResultPacketLen;
      result.workDirectDrainFirstRejectedResultConnHandle =
          workDrainStats.firstRejectedResultConnHandle;
      result.workDirectDrainFirstRejectedResultProcedureCounter =
          workDrainStats.firstRejectedResultProcedureCounter;
      result.workDirectDrainLastEvent = workDrainStats.lastEventCode;
      result.workDirectDrainLastSubevent = workDrainStats.lastLeSubeventCode;
      result.workDirectDrainLastVendor = workDrainStats.lastVendorSubeventCode;
      result.workDirectDrainFirstRejectedEvent =
          workDrainStats.firstRejectedEventCode;
      result.workDirectDrainFirstRejectedSubevent =
          workDrainStats.firstRejectedLeSubeventCode;
      result.workDirectDrainFirstRejectedConfigId =
          workDrainStats.firstRejectedConfigId;
      result.workDirectDrainFirstRejectedSteps =
          workDrainStats.firstRejectedSteps;
      result.workDirectDrainFirstRejectedProcedureDone =
          workDrainStats.firstRejectedProcedureDoneStatus;
      result.workDirectDrainFirstRejectedSubeventDone =
          workDrainStats.firstRejectedSubeventDoneStatus;
      result.workDirectDrainFirstRejectedResultSubevent =
          workDrainStats.firstRejectedResultLeSubeventCode;
      result.workDirectDrainFirstRejectedResultConfigId =
          workDrainStats.firstRejectedResultConfigId;
      result.workDirectDrainFirstRejectedResultSteps =
          workDrainStats.firstRejectedResultSteps;
      result.workDirectDrainFirstRejectedResultProcedureDone =
          workDrainStats.firstRejectedResultProcedureDoneStatus;
      result.workDirectDrainFirstRejectedResultSubeventDone =
          workDrainStats.firstRejectedResultSubeventDoneStatus;
    }
    updateWorkHostResultDeltas();
  };
  if (host != nullptr && workItemApplied) {
    result.workExecuteAttempted = true;
    BleCsVprMeasurementExecutionResult execution{};
    workHostStateBefore = host->hostState();
    workHostStateBaselineValid = true;
    const bool workExecuteDirectOk =
        host->readMeasurementExecutionSnapshot(&execution);
    captureWorkDrainStats();
    if (workExecuteDirectOk) {
      if (workHostLocalResultPacketDelta == 0U ||
          workHostPeerResultPacketDelta == 0U ||
          workHostPeerResultMarkerDelta == 0U ||
          !host->estimateValid()) {
        (void)host->drainPendingConnectedControllerEvents();
        captureWorkDrainStats();
      }
    }
    if (workExecuteDirectOk) {
      result.workExecutedChannelCount = execution.executedChannelCount;
      result.workExecutionToken = execution.executionToken;
      result.workControllerOwnedSnapshot = execution.controllerOwnedSnapshot;
      bool executionChannelsMatchWork =
          execution.stepChannelCount == work->stepChannelCount &&
          execution.executedChannelCount == effectiveChannelCount;
      for (uint8_t i = 0U;
           i < sizeof(execution.stepChannels) && i < sizeof(work->stepChannels);
           ++i) {
        if (execution.stepChannels[i] != work->stepChannels[i]) {
          executionChannelsMatchWork = false;
          break;
        }
      }
      const uint32_t expectedExecutionToken = buildMeasurementExecuteToken(
          execution.configId, execution.procedureCounter, execution.connHandle,
          execution.activeSubeventIndex, execution.totalSubevents,
          execution.totalSteps, execution.subeventStartStep,
          execution.subeventStepCount, execution.stepChannelCount,
          execution.stepChannels, execution.executeCount);
      result.workExecuteTokenOk =
          execution.executionTokenValid &&
          execution.executionToken == expectedExecutionToken;
      const uint32_t expectedRfDescriptorToken =
          buildMeasurementRfDescriptorToken(
              execution.configId, execution.procedureCounter,
              execution.connHandle, execution.activeSubeventIndex,
              execution.totalSubevents, execution.totalSteps,
              execution.subeventStartStep, execution.subeventStepCount,
              execution.stepChannelCount, execution.stepChannels,
              execution.rfRole, execution.rfPhy, execution.rfTxPowerDelta,
              execution.rfRttType, execution.rfMinSubeventLen,
              execution.rfMaxSubeventLen, execution.executeCount);
      const uint32_t expectedRfHardwareToken =
          buildMeasurementRfHardwareToken(
              execution.rfHardwareVersion, execution.rfHardwareFlags,
              execution.rfHardwareState, execution.rfHardwareMode,
              execution.rfHardwareFrequency);
      const uint32_t expectedRfPrimitiveToken =
          buildMeasurementRfPrimitiveToken(
              execution.rfPrimitiveVersion, execution.rfPrimitiveFlags,
              execution.rfPrimitiveStatus, execution.rfPrimitiveStateBefore,
              execution.rfPrimitivePllWaitLoops,
              execution.rfPrimitiveDisableWaitLoops,
              execution.rfPrimitiveStateAfter);
      const uint8_t expectedRetuneChannel =
          (effectiveChannelCount > 0U) ? effectiveChannels[0] : 0xFFU;
      const uint32_t expectedRetuneFrequency =
          validLogicalChannel(expectedRetuneChannel)
              ? static_cast<uint32_t>(logicalChannelToFrequency(
                    expectedRetuneChannel))
              : 0xFFFFFFFFUL;
      const uint32_t expectedRetuneDatawhite =
          validLogicalChannel(expectedRetuneChannel)
              ? bleDataWhiteValue(expectedRetuneChannel)
              : 0U;
      const uint32_t expectedRfRetuneToken =
          buildMeasurementRfRetuneToken(
              execution.rfRetuneVersion, execution.rfRetuneFlags,
              execution.rfRetuneStatus, execution.rfRetuneChannel,
              execution.rfRetuneTargetFrequency,
              execution.rfRetuneTargetDatawhite,
              execution.rfRetuneObservedFrequency,
              execution.rfRetuneObservedDatawhite);
      const uint32_t expectedRfRxPrimitiveToken =
          buildMeasurementRfPrimitiveToken(
              execution.rfRxPrimitiveVersion, execution.rfRxPrimitiveFlags,
              execution.rfRxPrimitiveStatus,
              execution.rfRxPrimitiveStateBefore,
              execution.rfRxPrimitiveRxReadyWaitLoops,
              execution.rfRxPrimitiveDisableWaitLoops,
              execution.rfRxPrimitiveStateAfter);
      const uint8_t expectedRfPacketConfigFlags = 0xFFU;
      const uint8_t expectedRfPacketConfigStatus = 0U;
      const bool rfPacketParamsValid = execution.rfPacketParamsValid;
      const uint8_t expectedRfPacketS0 =
          rfPacketParamsValid ? execution.rfPacketS0
                              : config.radioConfig.s0Pattern;
      const uint8_t expectedRfPacketCteInfo =
          rfPacketParamsValid
              ? execution.rfPacketCteInfo
              : static_cast<uint8_t>((kCteTypeAoA << 6U) |
                                     (config.radioConfig.cteTimeUnits & 0x1FU));
      const uint8_t expectedRfPacketPayloadLen =
          rfPacketParamsValid ? execution.rfPacketPayloadLen
                              : kPayloadHeaderLen;
      const uint8_t expectedRfPacketMagic0 =
          rfPacketParamsValid ? execution.rfPacketMagic0 : kMagic0;
      const uint8_t expectedRfPacketMagic1 =
          rfPacketParamsValid ? execution.rfPacketMagic1 : kMagic1;
      const uint8_t expectedRfPacketType =
          rfPacketParamsValid ? execution.rfPacketType
                              : kBleCsVprPacketTypeProbe;
      const uint8_t expectedRfPacketSequence =
          rfPacketParamsValid
              ? execution.rfPacketSequence
              : static_cast<uint8_t>(execution.executeCount & 0xFFU);
      const uint8_t expectedRfPacketChannel =
          rfPacketParamsValid ? execution.rfPacketChannel
                              : expectedRetuneChannel;
      const uint16_t expectedRfControlToProbeDelayUs =
          rfPacketParamsValid ? execution.rfPacketControlToProbeDelayUs
                              : config.radioConfig.controlToProbeDelayUs;
      const uint16_t expectedRfResponseListenWindowUs =
          rfPacketParamsValid ? execution.rfPacketResponseListenWindowUs
                              : config.radioConfig.responseListenWindowUs;
      result.workRfDescriptorToken = execution.rfDescriptorToken;
      result.workExecutionStatus = execution.status;
      result.workExecutionFlags = execution.flags;
      result.workRfHardwareToken = execution.rfHardwareToken;
      result.workRfHardwareState = execution.rfHardwareState;
      result.workRfHardwareMode = execution.rfHardwareMode;
      result.workRfHardwareFrequency = execution.rfHardwareFrequency;
      result.workRfPrimitiveToken = execution.rfPrimitiveToken;
      result.workRfPrimitiveStatus = execution.rfPrimitiveStatus;
      result.workRfPrimitiveFlags = execution.rfPrimitiveFlags;
      result.workRfPrimitiveStateBefore = execution.rfPrimitiveStateBefore;
      result.workRfPrimitivePllWaitLoops = execution.rfPrimitivePllWaitLoops;
      result.workRfPrimitiveDisableWaitLoops =
          execution.rfPrimitiveDisableWaitLoops;
      result.workRfPrimitiveStateAfter = execution.rfPrimitiveStateAfter;
      result.workRfRetuneToken = execution.rfRetuneToken;
      result.workRfRetuneStatus = execution.rfRetuneStatus;
      result.workRfRetuneFlags = execution.rfRetuneFlags;
      result.workRfRetuneChannel = execution.rfRetuneChannel;
      result.workRfRetuneTargetFrequency = execution.rfRetuneTargetFrequency;
      result.workRfRetuneTargetDatawhite = execution.rfRetuneTargetDatawhite;
      result.workRfRetuneObservedFrequency =
          execution.rfRetuneObservedFrequency;
      result.workRfRetuneObservedDatawhite =
          execution.rfRetuneObservedDatawhite;
      result.workRfRxPrimitiveToken = execution.rfRxPrimitiveToken;
      result.workRfRxPrimitiveStatus = execution.rfRxPrimitiveStatus;
      result.workRfRxPrimitiveFlags = execution.rfRxPrimitiveFlags;
      result.workRfRxPrimitiveStateBefore =
          execution.rfRxPrimitiveStateBefore;
      result.workRfRxPrimitiveRxReadyWaitLoops =
          execution.rfRxPrimitiveRxReadyWaitLoops;
      result.workRfRxPrimitiveDisableWaitLoops =
          execution.rfRxPrimitiveDisableWaitLoops;
      result.workRfRxPrimitiveStateAfter = execution.rfRxPrimitiveStateAfter;
      result.workRfPacketConfigToken = execution.rfPacketConfigToken;
      result.workRfPacketConfigPcnf0 = kBleCsVprPacketPcnf0Default;
      result.workRfPacketConfigPcnf1 = kBleCsVprPacketPcnf1Default;
      result.workRfPacketConfigStatus = expectedRfPacketConfigStatus;
      result.workRfPacketConfigFlags = expectedRfPacketConfigFlags;
      result.workRfPacketConfigMaxPayload = kBleCsVprPacketMaxPayloadDefault;
      result.workRfPacketS0 = expectedRfPacketS0;
      result.workRfPacketCteInfo = expectedRfPacketCteInfo;
      result.workRfPacketPayloadLen = expectedRfPacketPayloadLen;
      result.workRfPacketMagic0 = expectedRfPacketMagic0;
      result.workRfPacketMagic1 = expectedRfPacketMagic1;
      result.workRfPacketType = expectedRfPacketType;
      result.workRfPacketSequence = expectedRfPacketSequence;
      result.workRfPacketChannel = expectedRfPacketChannel;
      result.workRfPacketControlToProbeDelayUs =
          expectedRfControlToProbeDelayUs;
      result.workRfPacketResponseListenWindowUs =
          expectedRfResponseListenWindowUs;
      result.workRfTimedMode2Token = execution.rfTimedMode2Token;
      result.workRfTimedMode2Status = execution.rfTimedMode2Status;
      result.workRfTimedMode2Flags = execution.rfTimedMode2Flags;
      result.workRfTimedMode2Channel = execution.rfTimedMode2Channel;
      result.workRfTimedMode2TxWaitLoops =
          execution.rfTimedMode2TxWaitLoops;
      result.workRfTimedMode2GapWaitLoops =
          execution.rfTimedMode2GapWaitLoops;
      result.workRfTimedMode2RxReadyWaitLoops =
          execution.rfTimedMode2RxReadyWaitLoops;
      result.workRfTimedMode2ListenWaitLoops =
          execution.rfTimedMode2ListenWaitLoops;
      result.workRfTimedMode2DisableWaitLoops =
          execution.rfTimedMode2DisableWaitLoops;
      result.workRfTimedMode2StateAfter = execution.rfTimedMode2StateAfter;
      result.workRfTimingOwnerToken = execution.rfTimingOwnerToken;
      result.workRfTimingOwnerStatus = execution.rfTimingOwnerStatus;
      result.workRfTimingOwnerFlags = execution.rfTimingOwnerFlags;
      result.workRfTimingOwnerSubevent =
          execution.rfTimingOwnerActiveSubevent;
      result.workRfTimingOwnerHeartbeat =
          execution.rfTimingOwnerHeartbeat;
      result.workRfTimingOwnerNextProcedureHeartbeat =
          execution.rfTimingOwnerNextProcedureHeartbeat;
      result.workRfTimingOwnerNextSubeventHeartbeat =
          execution.rfTimingOwnerNextSubeventHeartbeat;
      result.workRfTimingOwnerProcedureIntervalTicks =
          execution.rfTimingOwnerProcedureIntervalTicks;
      result.workRfTimingOwnerSubeventDelayTicks =
          execution.rfTimingOwnerSubeventDelayTicks;
      result.workRfTimingOwnerPeerGapTicks =
          execution.rfTimingOwnerPeerGapTicks;
      result.workRfTimingOwnerIntervalSelector =
          execution.rfTimingOwnerIntervalSelector;
      result.workRfTimedMode2ObservedCount =
          execution.rfTimedMode2ObservedCount;
      memcpy(result.workRfTimedMode2ObservedChannels,
             execution.rfTimedMode2ObservedChannels,
             sizeof(result.workRfTimedMode2ObservedChannels));
      memcpy(result.workRfTimedMode2ObservedStatus,
             execution.rfTimedMode2ObservedStatus,
             sizeof(result.workRfTimedMode2ObservedStatus));
      memcpy(result.workRfTimedMode2ObservedFlags,
             execution.rfTimedMode2ObservedFlags,
             sizeof(result.workRfTimedMode2ObservedFlags));
      memcpy(result.workRfTimedMode2ObservedEventMask,
             execution.rfTimedMode2ObservedEventMask,
             sizeof(result.workRfTimedMode2ObservedEventMask));
      memcpy(result.workRfTimedMode2ObservedTokens,
             execution.rfTimedMode2ObservedTokens,
             sizeof(result.workRfTimedMode2ObservedTokens));
      result.workRfMaxSubeventLen = execution.rfMaxSubeventLen;
      result.workRfPhy = execution.rfPhy;
      result.workRfTxPowerDelta = execution.rfTxPowerDelta;
      result.workRfDescriptorOk =
          execution.rfDescriptorValid &&
          execution.rfDescriptorTokenValid &&
          execution.rfDescriptorToken == expectedRfDescriptorToken &&
          execution.rfStepChannelCount == execution.stepChannelCount &&
          execution.rfStepChannelCount == work->stepChannelCount;
      result.workRfHardwareOk =
          execution.rfHardwareValid &&
          execution.rfHardwareTokenValid &&
          execution.rfHardwareToken == expectedRfHardwareToken;
      result.workRfPrimitiveOk =
          execution.rfPrimitiveValid &&
          execution.rfPrimitiveTokenValid &&
          execution.rfPrimitiveToken == expectedRfPrimitiveToken &&
          execution.rfPrimitiveStatus == 0U &&
          execution.rfPrimitivePllReady &&
          execution.rfPrimitiveDisabled &&
          execution.rfPrimitiveStateBefore == 0U &&
          execution.rfPrimitiveStateAfter == 0U;
      result.workRfRetuneOk =
          execution.rfRetuneValid &&
          execution.rfRetuneTokenValid &&
          execution.rfRetuneToken == expectedRfRetuneToken &&
          execution.rfRetuneStatus == 0U &&
          execution.rfRetuneModeWritten &&
          execution.rfRetuneFrequencyWritten &&
          execution.rfRetuneDatawhiteWritten &&
          execution.rfRetuneChannel == expectedRetuneChannel &&
          execution.rfRetuneTargetFrequency == expectedRetuneFrequency &&
          execution.rfRetuneObservedFrequency == expectedRetuneFrequency &&
          execution.rfRetuneTargetDatawhite == expectedRetuneDatawhite &&
          execution.rfRetuneObservedDatawhite == expectedRetuneDatawhite;
      result.workRfRxPrimitiveOk =
          execution.rfRxPrimitiveValid &&
          execution.rfRxPrimitiveTokenValid &&
          execution.rfRxPrimitiveToken == expectedRfRxPrimitiveToken &&
          execution.rfRxPrimitiveStatus == 0U &&
          execution.rfRxPrimitiveRxReady &&
          execution.rfRxPrimitiveDisabled &&
          execution.rfRxPrimitiveStateBefore == 0U &&
          execution.rfRxPrimitiveStateAfter == 0U;
      result.workRfPacketConfigOk =
          execution.rfPacketConfigTokenValid &&
          execution.rfPacketConfigToken ==
              buildMeasurementRfPacketConfigToken(
                  1U, expectedRfPacketConfigFlags,
                  expectedRfPacketConfigStatus,
                  result.workRfPacketConfigPcnf0,
                  result.workRfPacketConfigPcnf1,
                  expectedRfPacketS0,
                  expectedRfPacketPayloadLen,
                  expectedRfPacketCteInfo,
                  expectedRfPacketMagic0,
                  expectedRfPacketMagic1,
                  expectedRfPacketType,
                  expectedRfPacketSequence,
                  expectedRfPacketChannel,
                  expectedRfControlToProbeDelayUs,
                  expectedRfResponseListenWindowUs) &&
          (!rfPacketParamsValid ||
           (!execution.controllerOwnedSnapshot ||
            (execution.rfPacketParamsControllerOwned &&
             expectedRfPacketPayloadLen == kPayloadHeaderLen &&
             expectedRfPacketMagic0 == kMagic0 &&
             expectedRfPacketMagic1 == kMagic1 &&
             expectedRfPacketType == kBleCsVprPacketTypeProbe &&
             expectedRfPacketChannel == expectedRetuneChannel)));
      result.workRfPacketBufferOk = result.workRfPacketConfigOk;
      const uint8_t requiredTimedMode2Flags =
          0x01U | 0x02U | 0x04U | 0x08U | 0x20U | 0x40U | 0x80U;
      result.workRfTimedMode2Ok =
          execution.rfTimedMode2Valid &&
          execution.rfTimedMode2TokenValid &&
          execution.rfTimedMode2Status == 0U &&
          (execution.rfTimedMode2Flags & requiredTimedMode2Flags) ==
              requiredTimedMode2Flags &&
          execution.rfTimedMode2Channel == expectedRfPacketChannel &&
          execution.rfTimedMode2StateAfter == 0U &&
          execution.rfTimedMode2GapWaitLoops != 0U &&
          execution.rfTimedMode2RxReadyWaitLoops != 0U &&
          execution.rfTimedMode2Token ==
              buildMeasurementRfTimedMode2Token(
                  1U,
                  execution.rfTimedMode2Flags,
                  execution.rfTimedMode2Status,
                  expectedRfPacketChannel,
                  expectedRfPacketSequence,
                  expectedRfControlToProbeDelayUs,
                  expectedRfResponseListenWindowUs,
                  execution.rfTimedMode2TxWaitLoops,
                  execution.rfTimedMode2GapWaitLoops,
                  execution.rfTimedMode2RxReadyWaitLoops,
                  execution.rfTimedMode2ListenWaitLoops,
          execution.rfTimedMode2DisableWaitLoops,
          execution.rfTimedMode2StateAfter);
      const uint8_t requiredTimingOwnerFlags =
          0x01U | 0x02U | 0x04U | 0x08U | 0x20U;
      const bool timingOwnerSubeventMatchesWork =
          execution.rfTimingOwnerActiveSubevent == work->activeSubeventIndex ||
          execution.totalSubevents <= 1U ||
          work->totalSubevents <= 1U;
      result.workRfTimingOwnerOk =
          execution.rfTimingOwnerValid &&
          execution.rfTimingOwnerTokenValid &&
          execution.rfTimingOwnerStatus == 0U &&
          (execution.rfTimingOwnerFlags & requiredTimingOwnerFlags) ==
              requiredTimingOwnerFlags &&
          execution.rfTimingOwnerProcedureCounter == work->procedureCounter &&
          execution.rfTimingOwnerConnHandle == work->connHandle &&
          timingOwnerSubeventMatchesWork &&
          execution.rfTimingOwnerProcedureIntervalTicks != 0U &&
          execution.rfTimingOwnerSubeventDelayTicks != 0U &&
          execution.rfTimingOwnerToken ==
              buildMeasurementRfTimingOwnerToken(
                  execution.rfTimingOwnerVersion,
                  execution.rfTimingOwnerFlags,
                  execution.rfTimingOwnerStatus,
                  execution.rfTimingOwnerActiveSubevent,
                  execution.rfTimingOwnerProcedureCounter,
                  execution.rfTimingOwnerConnHandle,
                  execution.rfTimingOwnerHeartbeat,
                  execution.rfTimingOwnerNextProcedureHeartbeat,
                  execution.rfTimingOwnerNextSubeventHeartbeat,
                  execution.rfTimingOwnerProcedureIntervalTicks,
                  execution.rfTimingOwnerSubeventDelayTicks,
                  execution.rfTimingOwnerPeerGapTicks,
                  execution.rfTimingOwnerIntervalSelector);
      bool observedAllChannelsOk =
          execution.rfTimedMode2ObservedCount == effectiveChannelCount;
      if (effectiveChannelCount >
          sizeof(execution.rfTimedMode2ObservedChannels)) {
        observedAllChannelsOk = false;
      }
      for (uint8_t i = 0U; observedAllChannelsOk && i < effectiveChannelCount;
           ++i) {
        bool foundObservedChannel = false;
        for (uint8_t j = 0U;
             j < execution.rfTimedMode2ObservedCount &&
             j < sizeof(execution.rfTimedMode2ObservedChannels);
             ++j) {
          if (execution.rfTimedMode2ObservedChannels[j] == effectiveChannels[i] &&
              execution.rfTimedMode2ObservedStatus[j] == 0U &&
              execution.rfTimedMode2ObservedTokens[j] != 0U &&
              (execution.rfTimedMode2ObservedFlags[j] & 0x01U) != 0U) {
            foundObservedChannel = true;
            break;
          }
        }
        if (!foundObservedChannel) {
          observedAllChannelsOk = false;
        }
      }
      const bool activeSubeventMatchesWork =
          execution.activeSubeventIndex == work->activeSubeventIndex ||
          execution.totalSubevents <= 1U ||
          work->totalSubevents <= 1U;
      uint32_t executionMismatchMask = 0U;
      if (!(execution.valid && execution.status == 0U && execution.accepted)) {
        executionMismatchMask |= (1UL << 0U);
      }
      if (execution.procedureCounter != work->procedureCounter) {
        executionMismatchMask |= (1UL << 1U);
      }
      if (execution.configId != work->configId) {
        executionMismatchMask |= (1UL << 2U);
      }
      if (!activeSubeventMatchesWork) {
        executionMismatchMask |= (1UL << 3U);
      }
      if (execution.totalSubevents != work->totalSubevents) {
        executionMismatchMask |= (1UL << 4U);
      }
      if (execution.totalSteps != work->totalSteps) {
        executionMismatchMask |= (1UL << 5U);
      }
      if (execution.subeventStartStep != work->subeventStartStep) {
        executionMismatchMask |= (1UL << 6U);
      }
      if (execution.subeventStepCount != work->subeventStepCount) {
        executionMismatchMask |= (1UL << 7U);
      }
      if (!executionChannelsMatchWork) {
        executionMismatchMask |= (1UL << 8U);
      }
      if (!result.workExecuteTokenOk) {
        executionMismatchMask |= (1UL << 9U);
      }
      if (!result.workRfDescriptorOk) {
        executionMismatchMask |= (1UL << 10U);
      }
      if (!result.workRfHardwareOk) {
        executionMismatchMask |= (1UL << 11U);
      }
      if (!result.workRfPrimitiveOk) {
        executionMismatchMask |= (1UL << 12U);
      }
      if (!result.workRfRetuneOk) {
        executionMismatchMask |= (1UL << 13U);
      }
      if (!result.workRfRxPrimitiveOk) {
        executionMismatchMask |= (1UL << 14U);
      }
      if (!result.workRfPacketConfigOk) {
        executionMismatchMask |= (1UL << 15U);
      }
      if (!result.workRfPacketBufferOk) {
        executionMismatchMask |= (1UL << 16U);
      }
      if (!result.workRfTimedMode2Ok) {
        executionMismatchMask |= (1UL << 17U);
      }
      if (!observedAllChannelsOk) {
        executionMismatchMask |= (1UL << 18U);
      }
      if (!result.workRfTimingOwnerOk) {
        executionMismatchMask |= (1UL << 19U);
      }
      if (!result.workAutoExecuted) {
        executionMismatchMask |= (1UL << 20U);
      }
      if (!execution.controllerOwnedSnapshot) {
        executionMismatchMask |= (1UL << 21U);
      }
      if (result.workAutoStatus != 0U) {
        executionMismatchMask |= (1UL << 22U);
      }
      if (result.workAutoProcedureCounter != work->procedureCounter ||
          (result.workAutoSubevent != work->activeSubeventIndex &&
           work->totalSubevents > 1U)) {
        executionMismatchMask |= (1UL << 23U);
      }
      if (result.workAutoCount == 0U || result.workAutoDuePasses == 0U) {
        executionMismatchMask |= (1UL << 24U);
      }
      result.workExecuteMismatchMask = executionMismatchMask;
      const bool executionMatchesWork =
          execution.valid && execution.status == 0U && execution.accepted &&
          result.workAutoExecuted &&
          execution.controllerOwnedSnapshot &&
          result.workAutoStatus == 0U &&
          result.workAutoCount != 0U &&
          result.workAutoDuePasses != 0U &&
          result.workAutoProcedureCounter == work->procedureCounter &&
          (result.workAutoSubevent == work->activeSubeventIndex ||
           work->totalSubevents <= 1U) &&
          execution.procedureCounter == work->procedureCounter &&
          execution.configId == work->configId &&
          activeSubeventMatchesWork &&
          execution.totalSubevents == work->totalSubevents &&
          execution.totalSteps == work->totalSteps &&
          execution.subeventStartStep == work->subeventStartStep &&
          execution.subeventStepCount == work->subeventStepCount &&
          executionChannelsMatchWork &&
          result.workExecuteTokenOk &&
          result.workRfDescriptorOk &&
          result.workRfHardwareOk &&
          result.workRfPrimitiveOk &&
          result.workRfRetuneOk &&
          result.workRfRxPrimitiveOk &&
          result.workRfPacketConfigOk &&
          result.workRfPacketBufferOk &&
          result.workRfTimedMode2Ok &&
          result.workRfTimingOwnerOk &&
          observedAllChannelsOk;
      workExecutionOk = executionMatchesWork;
      result.workExecuteOk = executionMatchesWork;
    } else {
      workExecutionOk = false;
      result.workExecuteOk = false;
    }
  }

  for (uint8_t i = 0U; i < config.channelCount; ++i) {
    measurements[i] = BleCsChannelMeasurement{};
  }

  if (workItemApplied) {
    result.attempts = effectiveChannelCount;
    result.validChannels = result.workExecutedChannelCount;
  } else {
  for (uint8_t order = 0U; order < effectiveChannelCount; ++order) {
    BleCsConnectedMode2ChannelResult channelResult{};
    channelResult.channel = effectiveChannels[order];
    channelResult.order = order;
    ++result.attempts;

    BleChannelSoundingLlControlDebug dbgBefore{};
    ble.getChannelSoundingLlControlDebug(&dbgBefore);

    BleCsLlControlPdu triggerPdu{};
    if (bleCsBuildLlControlTerminate(config.triggerReason, &triggerPdu)) {
      channelResult.triggerQueued =
          ble.queueChannelSoundingLlControlPdu(triggerPdu.data(),
                                               triggerPdu.length);
    }

    if (channelResult.triggerQueued) {
      for (uint8_t attempt = 0U; attempt < config.triggerAckPolls; ++attempt) {
        BleConnectionEvent evt{};
        const bool ran = ble.pollConnectionEvent(&evt, config.pollSpinLimit);

        if (!channelResult.triggerSent &&
            ran && evt.txPacketSent && evt.txPayload != nullptr &&
            evt.txPayloadLength >= kBleCsLlControlTerminateAbortPduLength &&
            evt.txPayload[0U] == kBleCsLlCtrlTerminate &&
            evt.txPayload[2U] == config.triggerReason) {
          channelResult.triggerSent = true;
          channelResult.triggerEventCounter = evt.eventCounter;
        }

        BleChannelSoundingLlControlDebug dbg{};
        ble.getChannelSoundingLlControlDebug(&dbg);
        if (!channelResult.triggerSent &&
            dbg.lastTxOpcode == kBleCsLlCtrlTerminate &&
            dbg.txSentCount > dbgBefore.txSentCount) {
          channelResult.triggerSent = true;
          channelResult.triggerEventCounter = evt.eventCounter;
        }

        if (ran && bleCsEventIsConnectedSweepAck(evt, config.ackReason)) {
          channelResult.triggerAcked = true;
          channelResult.ackEventCounter = evt.eventCounter;
          break;
        }
      }
    }

    if (channelResult.triggerSent && channelResult.triggerAcked) {
      const uint16_t runAfterEvent =
          static_cast<uint16_t>(channelResult.triggerEventCounter +
                                config.windowEventOffset);
      const uint8_t maxPolls =
          static_cast<uint8_t>(config.windowEventOffset +
                               config.startEventPollSlack);
      for (uint8_t attempt = 0U; attempt < maxPolls; ++attempt) {
        BleConnectionEvent evt{};
        const bool ran = ble.pollConnectionEvent(&evt, config.pollSpinLimit);
        if (ran && evt.eventStarted &&
            bleCsEventCounterReached(evt.eventCounter, runAfterEvent)) {
          channelResult.startEventSeen = true;
          channelResult.runAfterEventCounter = evt.eventCounter;
          break;
        }
      }
    }

    if (channelResult.startEventSeen) {
      channelResult.rfPathEnabled =
          BoardControl::enableRfPath(config.antennaPath);
      channelResult.radioStarted =
          channelResult.rfPathEnabled && radio.begin(config.radioConfig);

      BleConnectionTimingSnapshot snapshot{};
      channelResult.snapshotValid =
          channelResult.radioStarted &&
          ble.getConnectionTimingSnapshot(&snapshot);

      if (channelResult.snapshotValid) {
        channelResult.channelOk =
            radio.measureConnectedWindowChannel(
                snapshot, channelResult.channel, (*sequence)++,
                config.singleChannelWindowUs, config.guardBeforeUs,
                config.guardAfterUs, &channelResult.window);
      }

      channelResult.dfeInfo = radio.lastDfeCaptureInfo();
      if (channelResult.channelOk && host != nullptr &&
          !result.workToneSnapshotOk) {
        if (workItemApplied &&
            (workHostLocalResultPacketDelta == 0U ||
             workHostPeerResultPacketDelta == 0U ||
             workHostPeerResultMarkerDelta == 0U)) {
          (void)host->drainPendingConnectedControllerEvents();
          captureWorkDrainStats();
        }
        BleCsVprToneSnapshotResult snapshotResult{};
        if (host->directReadToneSnapshotForTest(&snapshotResult)) {
          result.workToneSnapshotToken = snapshotResult.token;
          result.workToneSnapshotPct16 = snapshotResult.pct16;
          result.workToneSnapshotMagPhase = snapshotResult.magPhase;
          result.workToneSnapshotMagStd = snapshotResult.magStd;
          result.workToneSnapshotFrequency = snapshotResult.frequency;
          result.workToneSnapshotState = snapshotResult.state;
          result.workToneSnapshotCstonesEndEvent =
              snapshotResult.cstonesEndEvent;
          result.workToneSnapshotStatus = snapshotResult.status;
          result.workToneSnapshotFlags = snapshotResult.flags;
          result.workToneTimedMode2Token = snapshotResult.timedMode2Token;
          result.workToneTimedMode2Status = snapshotResult.timedMode2Status;
          result.workToneTimedMode2Flags = snapshotResult.timedMode2Flags;
          result.workToneTimedMode2Channel = snapshotResult.timedMode2Channel;
          result.workToneTimedMode2PacketType =
              snapshotResult.timedMode2PacketType;
          result.workToneTimedMode2PacketChannel =
              snapshotResult.timedMode2PacketChannel;
          result.workToneTimedMode2EventMask =
              snapshotResult.timedMode2EventMask;
          result.workToneSnapshotOk =
              snapshotResult.valid &&
              snapshotResult.status == 0U &&
              snapshotResult.snapshotValid &&
              snapshotResult.tokenValid &&
              snapshotResult.sampleNonZero &&
              snapshotResult.toneConfigOk;
          result.workToneTimedMode2Ok =
              snapshotResult.timedMode2Valid &&
              snapshotResult.timedMode2TokenValid &&
              snapshotResult.timedMode2Status ==
                  result.workRfTimedMode2Status &&
              snapshotResult.timedMode2Token ==
                  result.workRfTimedMode2Token &&
              snapshotResult.timedMode2Channel ==
                  result.workRfTimedMode2Channel;
        }
      }
      radio.end();
    }

    channelResult.reason =
        channelResult.snapshotValid ? channelResult.window.reason : 11U;
    measurements[order] = channelResult.window.measurement;
    if (channelResult.channelOk) {
      ++result.validChannels;
    }
    result.lastChannel = channelResult;

    if (channelCallback != nullptr) {
      channelCallback(channelResult, channelCallbackUserData);
    }
  }

  result.rawEstimateValid =
      BleChannelSoundingRadio::estimateDistancePhaseSlope(
          measurements, effectiveChannelCount, &result.rawEstimate);
  }

  bool hostOk = true;
  if (host != nullptr) {
    const BleCsControllerHostState hostStateBefore = host->hostState();
    if (workItemApplied) {
      (void)host->drainPendingConnectedControllerEvents();
      captureWorkDrainStats();
      if (!host->estimateValid()) {
        (void)host->refreshEstimateFromCompletedResults();
      }
    } else {
      (void)host->drainPendingControllerEvents();
    }
    const auto completedResultMatchesWork = [&]() -> bool {
      uint32_t mismatchMask = 0U;
      result.workCompletedResultEstimateValid = host->estimateValid();
      const BleCsSubeventResult& localResult = host->completedLocalResult();
      const BleCsSubeventResult& peerResult = host->completedPeerResult();
      result.workCompletedResultConfigId = localResult.header.configId;
      result.workCompletedResultProcedureCounter =
          localResult.header.procedureCounter;
      result.workCompletedResultLocalSteps =
          localResult.header.numStepsReported;
      result.workCompletedResultPeerSteps =
          peerResult.header.numStepsReported;
      if (!workItemApplied) {
        result.workCompletedResultMismatchMask = 0xFFFFFFFFUL;
        return false;
      }
      if (!localResult.isComplete) {
        mismatchMask |= (1UL << 0U);
      }
      if (!peerResult.isComplete) {
        mismatchMask |= (1UL << 1U);
      }
      if (localResult.header.connHandle != work->connHandle) {
        mismatchMask |= (1UL << 2U);
      }
      if (peerResult.header.connHandle != work->connHandle) {
        mismatchMask |= (1UL << 3U);
      }
      if (localResult.header.configId != work->configId) {
        mismatchMask |= (1UL << 4U);
      }
      if (peerResult.header.configId != work->configId) {
        mismatchMask |= (1UL << 5U);
      }
      if (localResult.header.procedureCounter != work->procedureCounter) {
        mismatchMask |= (1UL << 6U);
      }
      if (peerResult.header.procedureCounter != work->procedureCounter) {
        mismatchMask |= (1UL << 7U);
      }
      if (localResult.header.numStepsReported != work->subeventStepCount) {
        mismatchMask |= (1UL << 8U);
      }
      if (peerResult.header.numStepsReported != work->subeventStepCount) {
        mismatchMask |= (1UL << 9U);
      }
      if (localResult.stepData == nullptr || localResult.stepDataLen == 0U) {
        mismatchMask |= (1UL << 10U);
      }
      if (peerResult.stepData == nullptr || peerResult.stepDataLen == 0U) {
        mismatchMask |= (1UL << 11U);
      }
      if (!result.workCompletedResultEstimateValid) {
        mismatchMask |= (1UL << 12U);
      }
      result.workCompletedResultMismatchMask = mismatchMask;
      return mismatchMask == 0U;
    };
    const bool completedWorkResultMatches = completedResultMatchesWork();
    const bool workHostResultIngress =
        workHostLocalResultPacketDelta > 0U &&
        workHostPeerResultPacketDelta > 0U &&
        workHostPeerResultMarkerDelta > 0U;
    const BleCsControllerHostState workHostStateForIngress = host->hostState();
    const bool workHostResultAlreadyPresent =
        workHostStateForIngress.localResultPackets > 0U &&
        workHostStateForIngress.peerResultPackets > 0U &&
        workHostStateForIngress.controllerPeerResultMarkers > 0U;
    if (workItemApplied) {
      /* Slice 4: connected-work results must arrive through VPR-published
       * controller events.  The controller can auto-publish before this
       * diagnostic runner reaches its execute baseline, so accept either a
       * fresh delta or already-present controller-owned result state.  Do not
       * satisfy this path by synthesizing result packets from the Arduino-side
       * measurement array. */
      hostOk =
          (workHostResultIngress || workHostResultAlreadyPresent) &&
          completedWorkResultMatches &&
          host->estimateValid();
    } else if (completedWorkResultMatches && host->estimateValid()) {
      hostOk = true;
    } else {
      hostOk =
          host->consumeConnectedMode2ControllerEventsFromMeasurements(
              measurements, effectiveChannelCount, effectiveConfigId, localStepData,
              localMaxStepDataLen, peerStepData, peerMaxStepDataLen,
              config.numAntennaPaths) &&
          host->estimateValid();
    }
    const BleCsControllerHostState hostStateAfter = host->hostState();
    if (host->estimateValid()) {
      result.workCompletedResultEstimateValid = true;
      result.workCompletedResultMismatchMask &= ~(1UL << 12U);
    }
    const bool completedWorkResultMatchesAfterEstimate =
        result.workCompletedResultMismatchMask == 0U;
    result.hostLocalResultPacketDelta =
        workHostLocalResultPacketDelta +
        hostStateAfter.localResultPackets - hostStateBefore.localResultPackets;
    result.hostPeerResultPacketDelta =
        workHostPeerResultPacketDelta +
        hostStateAfter.peerResultPackets - hostStateBefore.peerResultPackets;
    result.hostControllerEventPacketDelta =
        workHostControllerEventPacketDelta +
        hostStateAfter.controllerEventPackets - hostStateBefore.controllerEventPackets;
    result.hostPeerResultMarkerDelta =
        workHostPeerResultMarkerDelta +
        hostStateAfter.controllerPeerResultMarkers -
        hostStateBefore.controllerPeerResultMarkers;
    result.hostControllerResultIngress =
        (result.hostLocalResultPacketDelta > 0U &&
         result.hostPeerResultPacketDelta > 0U &&
         result.hostPeerResultMarkerDelta > 0U);
    if (workItemApplied && completedWorkResultMatchesAfterEstimate &&
        workHostResultAlreadyPresent) {
      result.hostControllerResultIngress = true;
    }
    if (!workItemApplied && completedWorkResultMatchesAfterEstimate) {
      result.hostControllerResultIngress = true;
    }
    hostOk = hostOk && result.hostControllerResultIngress;
    result.hostEstimateValid = hostOk;
    result.hostConfigId =
        hostOk ? host->completedLocalResult().header.configId : 0U;
    result.hostProcedureCounter =
        hostOk ? host->completedLocalResult().header.procedureCounter : 0U;
    result.hostLocalSteps =
        hostOk ? host->completedLocalResult().header.numStepsReported : 0U;
    result.hostPeerSteps =
        hostOk ? host->completedPeerResult().header.numStepsReported : 0U;
    if (result.workRfTimedMode2Ok) {
      result.workResultTimedMode2Channel = result.workRfTimedMode2Channel;
      result.workResultTimedMode2LocalOk =
          bleCsSubeventResultContainsTimedMode2Observation(
              host->completedLocalResult(), result.workRfTimedMode2Token,
              result.workRfTimedMode2Channel, false);
      result.workResultTimedMode2PeerOk =
          bleCsSubeventResultContainsTimedMode2Observation(
              host->completedPeerResult(), result.workRfTimedMode2Token,
              result.workRfTimedMode2Channel, true);
      result.workResultTimedMode2Ok =
          result.workResultTimedMode2LocalOk &&
          result.workResultTimedMode2PeerOk;
      result.workResultTimedMode2RequiredChannels =
          result.workRfTimedMode2ObservedCount;
      for (uint8_t i = 0U;
           i < result.workRfTimedMode2ObservedCount &&
           i < sizeof(result.workRfTimedMode2ObservedChannels);
           ++i) {
        const uint8_t channel = result.workRfTimedMode2ObservedChannels[i];
        const uint32_t token = result.workRfTimedMode2ObservedTokens[i];
        if (result.workRfTimedMode2ObservedStatus[i] != 0U ||
            token == 0U ||
            (result.workRfTimedMode2ObservedFlags[i] & 0x01U) == 0U) {
          continue;
        }
        if (bleCsSubeventResultContainsTimedMode2Observation(
                host->completedLocalResult(), token, channel, false)) {
          ++result.workResultTimedMode2LocalMatches;
        }
        if (bleCsSubeventResultContainsTimedMode2Observation(
                host->completedPeerResult(), token, channel, true)) {
          ++result.workResultTimedMode2PeerMatches;
        }
      }
      result.workResultTimedMode2AllChannelsOk =
          result.workResultTimedMode2RequiredChannels > 0U &&
          result.workResultTimedMode2LocalMatches ==
              result.workResultTimedMode2RequiredChannels &&
          result.workResultTimedMode2PeerMatches ==
              result.workResultTimedMode2RequiredChannels;
      result.workResultTimedMode2Ok =
          result.workResultTimedMode2Ok &&
          result.workResultTimedMode2AllChannelsOk;
    }
  } else {
    result.hostEstimateValid = false;
  }

  const bool enoughChannels = result.validChannels >= config.minValidChannels;
  const bool rawToneProofRequired = host != nullptr && !workItemApplied;
  result.ok = enoughChannels &&
              workExecutionOk &&
              (!rawToneProofRequired || result.workToneSnapshotOk) &&
              (!rawToneProofRequired || result.workToneTimedMode2Ok) &&
              (host == nullptr || result.workResultTimedMode2Ok) &&
              (host == nullptr || hostOk);
  *outResult = result;
  return result.ok;
}

bool BleChannelSoundingRadio::measureMode2Sweep(
    uint8_t channelCount,
    uint8_t* inOutSequence,
    BleCsChannelMeasurement* outMeasurements,
    uint8_t* outValidChannels,
    uint16_t interChannelGuardUs) {
  if (!initialized_ || inOutSequence == nullptr || outMeasurements == nullptr ||
      channelCount == 0U || channelCount > kMaxCsChannels) {
    if (outValidChannels != nullptr) {
      *outValidChannels = 0U;
    }
    return false;
  }

  uint8_t validChannels = 0U;
  for (uint8_t order = 0U; order < channelCount; ++order) {
    const uint8_t channel = centeredDataChannelAt(order, channelCount);
    BleCsChannelMeasurement measurement{};
    bool ok = false;
    if (validDataChannel(channel)) {
      ok = measureChannel(channel, *inOutSequence, &measurement);
      *inOutSequence = static_cast<uint8_t>(*inOutSequence + 1U);
    }
    outMeasurements[order] = measurement;
    if (ok && measurement.valid) {
      ++validChannels;
    }
    if (interChannelGuardUs > 0U) {
      waitElapsedMicros(interChannelGuardUs);
    }
  }

  if (outValidChannels != nullptr) {
    *outValidChannels = validChannels;
  }
  return validChannels > 0U;
}

bool BleChannelSoundingRadio::listenAndReflectOnce(uint32_t controlListenWindowUs) {
  if (!initialized_) {
    lastReflectorStatus_ = 1U;
    return false;
  }
  lastReflectorStatus_ = 0U;
  lastReflectorTiming_ = BleCsReflectorTiming{};
  const uint32_t attemptStartUs = micros();

  const uint32_t windowUs =
      (controlListenWindowUs != 0U) ? controlListenWindowUs
                                    : config_.controlListenWindowUs;

  RxFrame control{};
  if (!receiveFrame(config_.controlChannel, windowUs, false, false, false, &control,
                    nullptr, nullptr)) {
    lastReflectorTiming_.controlRxUs =
        static_cast<uint32_t>(micros() - attemptStartUs);
    lastReflectorStatus_ = 2U;
    return false;
  }
  lastReflectorTiming_.controlRxUs =
      static_cast<uint32_t>(micros() - attemptStartUs);
  if (!control.valid || control.type != PacketType::kControl ||
      !validDataChannel(control.channelIndex)) {
    lastReflectorStatus_ = 3U;
    return false;
  }

  lastReflectorTiming_.controlToProbeRxGapUs =
      static_cast<uint32_t>(micros() - attemptStartUs);
  RxFrame probe{};
  BleCsToneSample localTone{};
  BleCsRttSample localRtt{};
  if (!receiveFrame(control.channelIndex, config_.probeListenWindowUs, true,
                    config_.enableRtt, true, &probe, &localTone, &localRtt)) {
    lastReflectorTiming_.probeRxUs =
        static_cast<uint32_t>(micros() - attemptStartUs);
    lastReflectorStatus_ = 4U;
    return false;
  }
  lastReflectorTiming_.probeRxUs =
      static_cast<uint32_t>(micros() - attemptStartUs);
  if (!probe.valid || probe.type != PacketType::kProbe ||
      probe.sequence != control.sequence ||
      probe.channelIndex != control.channelIndex) {
    lastReflectorStatus_ = 5U;
    return false;
  }

  uint8_t reportExtra[kReportExtraLen] = {0};
  encodeReportExtra(localTone, reportExtra);
  encodeRttExtra(localRtt, &reportExtra[kReportToneExtraLen]);
  uint8_t flags = localTone.valid ? 0x01U : 0x00U;
  if (localRtt.present) {
    flags |= 0x02U;
  }
  if (config_.probeToReportDelayUs > 0U) {
    waitElapsedMicros(config_.probeToReportDelayUs);
  }
  lastReflectorTiming_.probeToReportGapUs =
      static_cast<uint32_t>(micros() - attemptStartUs);

  const bool sent = sendFrame(probe.channelIndex, PacketType::kReport,
                              probe.sequence, probe.channelIndex, flags,
                              reportExtra, sizeof(reportExtra),
                              config_.enableRtt, true);
  lastReflectorTiming_.reportTxUs =
      static_cast<uint32_t>(micros() - attemptStartUs);
  lastReflectorStatus_ = sent ? 0U : 6U;
  return sent;
}

float BleChannelSoundingRadio::combinedPhaseRad(
    const BleCsChannelMeasurement& measurement) {
  if (measurement.combinedPhaseValid) {
    return isfinite(measurement.combinedPhaseRad)
               ? measurement.combinedPhaseRad
               : NAN;
  }
  if (!measurement.localTone.valid || !measurement.peerTone.valid) {
    return 0.0f;
  }

  const float localI = static_cast<float>(measurement.localTone.i);
  const float localQ = static_cast<float>(measurement.localTone.q);
  const float peerI = static_cast<float>(measurement.peerTone.i);
  const float peerQ = static_cast<float>(measurement.peerTone.q);
  const float combI = (localI * peerI) - (localQ * peerQ);
  const float combQ = (localI * peerQ) + (peerI * localQ);
  return atan2f(combQ, combI);
}

bool BleChannelSoundingRadio::rttDistanceMeters(
    const BleCsChannelMeasurement& measurement, float* outDistanceMeters) {
  if (outDistanceMeters == nullptr) {
    return false;
  }

  *outDistanceMeters = 0.0f;
  if (!measurement.localRtt.valid || !measurement.peerRtt.valid) {
    return false;
  }

  const int32_t roundTripHalfNs =
      static_cast<int32_t>(measurement.localRtt.timeDifferenceHalfNs) -
      static_cast<int32_t>(measurement.peerRtt.timeDifferenceHalfNs);
  if (roundTripHalfNs <= 0) {
    return false;
  }

  const float tofNs = static_cast<float>(roundTripHalfNs) * 0.25f;
  const float distance =
      tofNs * (kSpeedOfLightMetersPerSecond / 1000000000.0f);
  if (!isfinite(distance) || distance <= 0.0f) {
    return false;
  }

  *outDistanceMeters = distance;
  return true;
}

bool BleChannelSoundingRadio::estimateDistancePhaseSlope(
    const BleCsChannelMeasurement* measurements, size_t count,
    BleCsEstimate* outEstimate) {
  if (measurements == nullptr || outEstimate == nullptr) {
    return false;
  }

  *outEstimate = BleCsEstimate{};
  outEstimate->phaseSlopeDistanceMeters = NAN;
  outEstimate->adjacentPhaseDistanceMeters = NAN;
  outEstimate->regressionPhaseDistanceMeters = NAN;
  outEstimate->rttDistanceMeters = NAN;
  outEstimate->distanceMeters = NAN;

  float freqsHz[kMaxCsChannels] = {0.0f};
  float phases[kMaxCsChannels] = {0.0f};
  float toneQuality[kMaxCsChannels] = {0.0f};
  size_t phaseCount = 0U;

  for (size_t i = 0U; i < count && phaseCount < kMaxCsChannels; ++i) {
    if (!measurements[i].valid || !measurements[i].localTone.valid ||
        !measurements[i].peerTone.valid || !validDataChannel(measurements[i].channelIndex)) {
      continue;
    }

    freqsHz[phaseCount] =
        (2400.0f + static_cast<float>(logicalChannelToFrequency(
                        measurements[i].channelIndex))) *
        1000000.0f;
    const float phase = combinedPhaseRad(measurements[i]);
    if (!isfinite(phase)) {
      continue;
    }
    phases[phaseCount] = phase;
    float quality =
        toneQualityScore(measurements[i].localTone, measurements[i].peerTone);
    if (measurements[i].combinedPhaseValid) {
      quality *= fmaxf(0.0f, fminf(1.0f, measurements[i].phaseCoherence));
    }
    toneQuality[phaseCount] = quality;
    ++phaseCount;
  }
  outEstimate->totalToneChannels = static_cast<uint8_t>(phaseCount);

  bool phaseOk = false;
  if (phaseCount >= 8U) {
    sortPhaseSamplesWithQuality(freqsHz, phases, toneQuality, phaseCount);

    float qualityScratch[kMaxCsChannels] = {0.0f};
    for (size_t i = 0U; i < phaseCount; ++i) {
      qualityScratch[i] = toneQuality[i];
    }
    const float medianQuality = medianInPlace(qualityScratch, phaseCount);
    outEstimate->medianToneQuality = medianQuality;

    const float qualityThreshold = fmaxf(0.35f, medianQuality * 0.35f);
    float qualityFreqs[kMaxCsChannels] = {0.0f};
    float qualityPhases[kMaxCsChannels] = {0.0f};
    float qualityScores[kMaxCsChannels] = {0.0f};
    size_t qualityCount = 0U;
    for (size_t i = 0U; i < phaseCount; ++i) {
      if (toneQuality[i] < qualityThreshold) {
        continue;
      }
      qualityFreqs[qualityCount] = freqsHz[i];
      qualityPhases[qualityCount] = phases[i];
      qualityScores[qualityCount] = toneQuality[i];
      ++qualityCount;
    }

    if (qualityCount >= 8U && qualityCount < phaseCount) {
      outEstimate->rejectedLowQualityChannels =
          static_cast<uint8_t>(phaseCount - qualityCount);
      phaseCount = qualityCount;
      for (size_t i = 0U; i < phaseCount; ++i) {
        freqsHz[i] = qualityFreqs[i];
        phases[i] = qualityPhases[i];
        toneQuality[i] = qualityScores[i];
      }
    }

    float adjacentDistance = NAN;
    float adjacentCoherence = 0.0f;
    uint8_t adjacentPairs = 0U;
    const bool adjacentOk = estimateAdjacentPhaseDistance(
        freqsHz, phases, toneQuality, phaseCount, &adjacentDistance,
        &adjacentCoherence, &adjacentPairs);
    outEstimate->adjacentPhaseDistanceMeters = adjacentDistance;
    outEstimate->adjacentPhaseCoherence = adjacentCoherence;
    outEstimate->adjacentPhasePairs = adjacentPairs;

    for (size_t i = 1U; i < phaseCount; ++i) {
      const float prev = phases[i - 1U];
      while ((phases[i] - prev) > kPi) {
        phases[i] -= 2.0f * kPi;
      }
      while ((phases[i] - prev) < -kPi) {
        phases[i] += 2.0f * kPi;
      }
    }

    bool regressionOk = false;
    float slope = 0.0f;
    float intercept = 0.0f;
    if (fitTheilSenLine(freqsHz, phases, phaseCount, &slope, &intercept)) {
      float residuals[kMaxCsChannels] = {0.0f};
      float absResiduals[kMaxCsChannels] = {0.0f};
      for (size_t i = 0U; i < phaseCount; ++i) {
        residuals[i] = phases[i] - (intercept + (slope * freqsHz[i]));
        absResiduals[i] = fabsf(residuals[i]);
      }

      const float mad = medianInPlace(absResiduals, phaseCount);
      const float inlierThreshold = fmaxf(0.45f, mad * 3.0f);

      float inlierFreqs[kMaxCsChannels] = {0.0f};
      float inlierPhases[kMaxCsChannels] = {0.0f};
      float inlierQuality[kMaxCsChannels] = {0.0f};
      size_t inlierCount = 0U;
      for (size_t i = 0U; i < phaseCount; ++i) {
        if (fabsf(residuals[i]) <= inlierThreshold) {
          inlierFreqs[inlierCount] = freqsHz[i];
          inlierPhases[inlierCount] = phases[i];
          inlierQuality[inlierCount] = toneQuality[i];
          ++inlierCount;
        }
      }

      if (inlierCount >= 8U && inlierCount < phaseCount &&
          fitTheilSenLine(inlierFreqs, inlierPhases, inlierCount, &slope, &intercept)) {
        outEstimate->rejectedResidualChannels =
            static_cast<uint8_t>(phaseCount - inlierCount);
        phaseCount = inlierCount;
        for (size_t i = 0U; i < phaseCount; ++i) {
          freqsHz[i] = inlierFreqs[i];
          phases[i] = inlierPhases[i];
          toneQuality[i] = inlierQuality[i];
        }
      }

      float refinedSlope = slope;
      float refinedIntercept = intercept;
      if (fitWeightedLine(freqsHz, phases, toneQuality, phaseCount, &refinedSlope,
                          &refinedIntercept)) {
        const float robustDistance = fabsf(
            -(kSpeedOfLightMetersPerSecond * slope) / (4.0f * kPi));
        const float refinedDistance = fabsf(
            -(kSpeedOfLightMetersPerSecond * refinedSlope) / (4.0f * kPi));
        outEstimate->fitDeltaMeters = fabsf(refinedDistance - robustDistance);
        slope = refinedSlope;
        intercept = refinedIntercept;
      }

      float residualSse = 0.0f;
      for (size_t i = 0U; i < phaseCount; ++i) {
        const float err = phases[i] - (intercept + (slope * freqsHz[i]));
        residualSse += err * err;
      }

      const float phaseDistance = fabsf(
          -(kSpeedOfLightMetersPerSecond * slope) / (4.0f * kPi));
      if (isfinite(phaseDistance) && phaseDistance > 0.0f) {
        regressionOk = true;
        outEstimate->usedChannels = static_cast<uint8_t>(phaseCount);
        outEstimate->regressionPhaseDistanceMeters = phaseDistance;
        outEstimate->slopeRadPerHz = slope;
        outEstimate->residualVariance =
            residualSse / static_cast<float>(phaseCount);
      }
    }

    phaseOk = adjacentOk || regressionOk;
    if (adjacentOk) {
      outEstimate->phaseSlopeDistanceMeters = adjacentDistance;
      if (!regressionOk) {
        outEstimate->usedChannels = static_cast<uint8_t>(phaseCount);
      }
    } else if (regressionOk) {
      outEstimate->phaseSlopeDistanceMeters =
          outEstimate->regressionPhaseDistanceMeters;
    }
  }

  float rttDistances[kMaxCsChannels] = {0.0f};
  size_t rttCount = 0U;
  for (size_t i = 0U; i < count && rttCount < kMaxCsChannels; ++i) {
    float distance = 0.0f;
    if (rttDistanceMeters(measurements[i], &distance)) {
      rttDistances[rttCount++] = distance;
    }
  }

  bool rttOk = false;
  if (rttCount >= 4U) {
    float rttMedian = 0.0f;
    float rttMad = 0.0f;
    if (estimateMedianAndMad(rttDistances, rttCount, &rttMedian, &rttMad)) {
      const float inlierThreshold = fmaxf(0.20f, rttMad * 3.0f);
      float inliers[kMaxCsChannels] = {0.0f};
      size_t inlierCount = 0U;
      for (size_t i = 0U; i < rttCount; ++i) {
        if (fabsf(rttDistances[i] - rttMedian) <= inlierThreshold) {
          inliers[inlierCount++] = rttDistances[i];
        }
      }

      if (inlierCount >= 3U &&
          estimateMedianAndMad(inliers, inlierCount, &rttMedian, &rttMad)) {
        float varianceAccum = 0.0f;
        for (size_t i = 0U; i < inlierCount; ++i) {
          const float err = inliers[i] - rttMedian;
          varianceAccum += err * err;
        }
        rttOk = true;
        outEstimate->rttChannels = static_cast<uint8_t>(inlierCount);
        outEstimate->rttDistanceMeters = rttMedian;
        outEstimate->rttVariance =
            varianceAccum / static_cast<float>(inlierCount);
      }
    }
  }

  if (!phaseOk && !rttOk) {
    return false;
  }

  outEstimate->valid = true;
  if (rttOk && phaseOk) {
    const float delta = fabsf(outEstimate->rttDistanceMeters -
                              outEstimate->phaseSlopeDistanceMeters);
    if (delta <= fmaxf(0.25f, sqrtf(outEstimate->rttVariance) + 0.20f)) {
      outEstimate->distanceMeters =
          (0.65f * outEstimate->rttDistanceMeters) +
          (0.35f * outEstimate->phaseSlopeDistanceMeters);
    } else {
      outEstimate->distanceMeters = outEstimate->rttDistanceMeters;
    }
  } else if (rttOk) {
    outEstimate->distanceMeters = outEstimate->rttDistanceMeters;
  } else {
    outEstimate->distanceMeters = outEstimate->phaseSlopeDistanceMeters;
  }

  return isfinite(outEstimate->distanceMeters) &&
         (outEstimate->distanceMeters > 0.0f);
}

}  // namespace xiao_nrf54l15
