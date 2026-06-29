/*
 * BleChannelSoundingLlControlWorkflowCentral
 *
 * Central-side workflow diagnostic for real over-air Channel Sounding
 * LL-control transport. Use with BleChannelSoundingLlControlPeripheral on a
 * second board.
 *
 * Unlike BleChannelSoundingLlControlCentral, this sketch does not manually
 * call each direct VPR HCI command. It lets BleCsControllerWorkflow pump the
 * normal Read Capabilities / Set Defaults / Create Config stages, then uses
 * loopOnceWithInitiatorLlControlBridge() so the over-air LL-control exchange
 * owns Security Enable, Procedure Parameters, and Procedure Enable ordering.
 *
 * Expected terminal output:
 *   cs_ll_workflow_bridge=PASS ...
 *   cs_distance_m=0.75 raw_m=0.75 confidence=high channels=3/3
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

#ifndef CS_AUTO_MEASUREMENT_PROOF_ONLY
#define CS_AUTO_MEASUREMENT_PROOF_ONLY 0
#endif

#ifndef CS_CONNECTED_PHYSICAL_CHANNEL_PROFILE
#define CS_CONNECTED_PHYSICAL_CHANNEL_PROFILE 0
#endif

#ifndef CS_CONNECTED_ENABLE_RTT
#define CS_CONNECTED_ENABLE_RTT 0
#endif

#ifndef CS_CALIBRATION_SCALE
#define CS_CALIBRATION_SCALE 1.0f
#endif

#ifndef CS_CALIBRATION_OFFSET_M
#define CS_CALIBRATION_OFFSET_M 0.0f
#endif

#ifndef CS_CALIBRATION_VALIDATED_MAD_M
#define CS_CALIBRATION_VALIDATED_MAD_M 0.0f
#endif

#ifndef CS_CALIBRATION_VALIDATED_P90_M
#define CS_CALIBRATION_VALIDATED_P90_M 0.0f
#endif

#ifndef CS_CALIBRATION_VALIDATED_SAMPLES
#define CS_CALIBRATION_VALIDATED_SAMPLES 0
#endif

namespace {

static BleRadio g_ble;
static PowerManager g_power;
static BleCsControllerVprHost g_csHost;
static BleCsControllerVprHostConfig g_csConfig{};
static BleCsLlControlBridgeWorkflowTracker g_bridgeTracker{};
static BleChannelSoundingRadio g_physicalCs;

static constexpr int8_t kTxPowerDbm = 0;
static constexpr uint16_t kCsConnHandle = 0x0041U;
static constexpr uint8_t kPeripheralAddress[6] = {
    0x11U, 0xC5U, 0x15U, 0x54U, 0xDEU, 0xC0U,
};
static constexpr uint32_t kStatusIntervalMs = 1000UL;
static constexpr BoardAntennaPath kPhysicalAntennaPath = BoardAntennaPath::kCeramic;
static constexpr uint8_t kPhysicalChannelCount = 37U;
static constexpr uint8_t kPhysicalMinValidChannels = 8U;
static constexpr uint8_t kPhysicalMaxSweeps = 3U;
static constexpr uint8_t kConnectedPhysicalSweepChannelsFast[] = {
    18U, 4U, 32U, 10U, 26U, 1U, 35U, 14U, 22U,
};
static constexpr uint8_t kConnectedPhysicalSweepChannelsWide[] = {
    18U, 4U, 32U, 10U, 26U, 1U, 35U, 14U, 22U,
    6U, 30U, 11U, 25U, 2U, 34U, 15U, 21U, 7U,
};
static constexpr uint8_t kConnectedPhysicalSweepChannelsFull[] = {
    18U, 4U, 32U, 10U, 26U, 1U, 35U, 14U, 22U,
    6U, 30U, 11U, 25U, 2U, 34U, 15U, 21U, 7U,
    29U, 12U, 24U, 3U, 33U, 16U, 20U, 8U, 28U,
    13U, 23U, 5U, 31U, 9U, 27U, 0U, 36U, 17U, 19U,
};
#if CS_CONNECTED_PHYSICAL_CHANNEL_PROFILE == 2
static constexpr const uint8_t* kConnectedPhysicalSweepChannels =
    kConnectedPhysicalSweepChannelsFull;
static constexpr uint8_t kConnectedPhysicalSweepChannelCount =
    sizeof(kConnectedPhysicalSweepChannelsFull) /
    sizeof(kConnectedPhysicalSweepChannelsFull[0]);
static constexpr uint8_t kConnectedPhysicalChannelProfile = 2U;
#elif CS_CONNECTED_PHYSICAL_CHANNEL_PROFILE == 1
static constexpr const uint8_t* kConnectedPhysicalSweepChannels =
    kConnectedPhysicalSweepChannelsWide;
static constexpr uint8_t kConnectedPhysicalSweepChannelCount =
    sizeof(kConnectedPhysicalSweepChannelsWide) /
    sizeof(kConnectedPhysicalSweepChannelsWide[0]);
static constexpr uint8_t kConnectedPhysicalChannelProfile = 1U;
#else
static constexpr const uint8_t* kConnectedPhysicalSweepChannels =
    kConnectedPhysicalSweepChannelsFast;
static constexpr uint8_t kConnectedPhysicalSweepChannelCount =
    sizeof(kConnectedPhysicalSweepChannelsFast) /
    sizeof(kConnectedPhysicalSweepChannelsFast[0]);
static constexpr uint8_t kConnectedPhysicalChannelProfile = 0U;
#endif
static constexpr uint8_t kConnectedPhysicalMinValidChannels = 3U;
static constexpr uint32_t kConnectedCsSingleChannelWindowUs = 14000UL;
static constexpr uint32_t kConnectedCsFullSweepWindowUs =
    kConnectedCsSingleChannelWindowUs * kPhysicalChannelCount;
static constexpr uint32_t kConnectedCsGuardBeforeUs = 4000UL;
static constexpr uint32_t kConnectedCsGuardAfterUs = 4500UL;
static constexpr uint8_t kConnectedPhysicalTriggerReason = 0x7EU;
static constexpr uint8_t kConnectedPhysicalAckReason = 0x7DU;
static constexpr uint16_t kConnectedPhysicalWindowEventOffset = 4U;
#if CS_AUTO_MEASUREMENT_PROOF_ONLY
static constexpr bool kAutoMeasurementProofOnly = true;
#else
static constexpr bool kAutoMeasurementProofOnly = false;
#endif
#if CS_CONNECTED_ENABLE_RTT
static constexpr bool kConnectedEnableRtt = true;
#else
static constexpr bool kConnectedEnableRtt = false;
#endif
static constexpr uint8_t kVprStageProcedureActive = 6U;
static constexpr uint8_t kVprWorkFlagControllerAutoExecuted = 0x80U;
static constexpr uint8_t kVprExecFlagControllerOwnedSnapshot = 0x10U;
static constexpr uint32_t kAutoMeasurementProofTimeoutMs = 30000UL;
static constexpr uint32_t kAutoMeasurementProofRetryDelayMs = 10UL;
static constexpr uint8_t kAutoMeasurementProofReadRetries = 20U;
static constexpr BleCsCalibrationProfile kConnectedCalibrationProfile = {
    CS_CALIBRATION_SCALE,
    CS_CALIBRATION_OFFSET_M,
    0.0f,
    0.0f,
    0.0f,
    CS_CALIBRATION_OFFSET_M,
    0.0f,
    0.0f,
    0.0f,
    CS_CALIBRATION_VALIDATED_MAD_M,
    CS_CALIBRATION_VALIDATED_P90_M,
    0U,
    static_cast<uint16_t>(CS_CALIBRATION_VALIDATED_SAMPLES),
};

static bool g_wasConnected = false;
static bool g_bridgeStarted = false;
static bool g_passPrinted = false;
static bool g_failed = false;
static bool g_autoProofPrinted = false;
static uint32_t g_autoProofStartedMs = 0U;
static bool g_physicalStarted = false;
static bool g_physicalReady = false;
static bool g_physicalDone = false;
static bool g_physicalFailed = false;
static bool g_connectedPhysicalAttempted = false;
static bool g_connectedPhysicalOk = false;
static uint8_t g_connectedPhysicalAttemptCount = 0U;
static uint8_t g_connectedPhysicalValidChannels = 0U;
static uint32_t g_connectAttempts = 0U;
static uint32_t g_linkEvents = 0U;
static uint32_t g_txQueued = 0U;
static uint32_t g_vprPduQueued = 0U;
static uint32_t g_peerPdusInjected = 0U;
static uint32_t g_directCommands = 0U;
static uint32_t g_lastStatusMs = 0U;
static uint32_t g_workflowMask = 0U;
static uint32_t g_txMask = 0U;
static uint32_t g_rxMask = 0U;
static uint8_t g_lastVprStage = 0xFFU;
static uint8_t g_lastVprStatus = 0xFFU;
static uint8_t g_physicalSequence = 0U;
static uint8_t g_physicalSweepCount = 0U;
static uint8_t g_physicalValidChannels = 0U;
static BleCsChannelMeasurement
    g_connectedPhysicalMeasurements[kConnectedPhysicalSweepChannelCount];
static uint8_t g_connectedPhysicalLocalStepData[kBleCsMaxControllerStepDataBytes];
static uint8_t g_connectedPhysicalPeerStepData[kBleCsMaxControllerStepDataBytes];
static BleCsChannelMeasurement g_physicalMeasurements[kPhysicalChannelCount];
static uint8_t g_physicalLocalStepData[kBleCsMaxControllerStepDataBytes];
static uint8_t g_physicalPeerStepData[kBleCsMaxControllerStepDataBytes];

enum WorkflowBits : uint8_t {
  kBitRemoteCaps = 0U,
  kBitDefaults,
  kBitConfig,
  kBitSecurity,
  kBitProcedureParams,
  kBitProcedureEnabled,
  kBitReady,
};

enum TxBits : uint8_t {
  kTxCsReq = 0U,
  kTxSecReq,
  kTxProcReq,
};

enum RxBits : uint8_t {
  kRxCsRsp = 0U,
  kRxCsCfg,
  kRxSecRsp,
  kRxProcRsp,
  kRxStart,
  kRxAbort,
};

static bool resultPathComplete() {
  const BleCsControllerHostState& host = g_csHost.hostState();
  const BleCsControllerSessionState& session = g_csHost.sessionState();
  return host.localSubeventResults > 0U &&
         host.peerSubeventResults > 0U &&
         session.completedProcedureCounter > 0U;
}

static void markBit(uint32_t* mask, uint8_t bit) {
  if (mask != nullptr && bit < 32U) {
    *mask |= (1UL << bit);
  }
}

static void printOpcode(uint8_t opcode) {
  Serial.print("0x");
  if (opcode < 16U) {
    Serial.print('0');
  }
  Serial.print(opcode, HEX);
}

static void printAddress(const uint8_t* address) {
  if (address == nullptr) {
    Serial.print("null");
    return;
  }

  for (int8_t i = 5; i >= 0; --i) {
    if (i != 5) {
      Serial.print(':');
    }
    if (address[i] < 16U) {
      Serial.print('0');
    }
    Serial.print(address[i], HEX);
  }
}

static void printDistanceField(const char* label, float value) {
  Serial.print(label);
  if (isfinite(value)) {
    Serial.print(value, 4);
  } else {
    Serial.print("nan");
  }
}

static uint8_t estimateConfidencePercent(const BleCsEstimate& estimate,
                                         uint8_t validChannels,
                                         uint8_t requestedChannels,
                                         uint8_t minValidChannels) {
  if (!estimate.valid || !isfinite(estimate.distanceMeters) ||
      !(estimate.distanceMeters > 0.0f)) {
    return 0U;
  }

  const float requested = (requestedChannels > 0U)
                              ? static_cast<float>(requestedChannels)
                              : 1.0f;
  const float channelRatio =
      static_cast<float>(validChannels) / requested;
  const float usedRatio =
      static_cast<float>(estimate.usedChannels) /
      fmaxf(static_cast<float>(minValidChannels), 1.0f);
  const float residualPenalty =
      isfinite(estimate.residualVariance)
          ? fminf(sqrtf(fmaxf(estimate.residualVariance, 0.0f)) / 0.75f, 1.0f)
          : 1.0f;
  const float fitPenalty =
      isfinite(estimate.fitDeltaMeters)
          ? fminf(estimate.fitDeltaMeters / 0.75f, 1.0f)
          : 1.0f;
  const float qualityScore =
      isfinite(estimate.medianToneQuality)
          ? fminf(fmaxf(estimate.medianToneQuality / 2.0f, 0.0f), 1.0f)
          : 0.5f;

  float score =
      (45.0f * fminf(channelRatio, 1.0f)) +
      (25.0f * fminf(usedRatio, 1.0f)) +
      (20.0f * (1.0f - residualPenalty)) +
      (10.0f * qualityScore);
  score -= 15.0f * fitPenalty;
  if (estimate.rejectedLowQualityChannels > 0U) {
    score -= fminf(static_cast<float>(estimate.rejectedLowQualityChannels) * 2.0f,
                   12.0f);
  }
  if (estimate.rejectedResidualChannels > 0U) {
    score -= fminf(static_cast<float>(estimate.rejectedResidualChannels) * 3.0f,
                   15.0f);
  }

  if (score < 0.0f) {
    score = 0.0f;
  }
  if (score > 100.0f) {
    score = 100.0f;
  }
  return static_cast<uint8_t>(score + 0.5f);
}

static const char* confidenceLabel(uint8_t confidence) {
  if (confidence >= 75U) {
    return "high";
  }
  if (confidence >= 45U) {
    return "medium";
  }
  if (confidence > 0U) {
    return "low";
  }
  return "invalid";
}

static void printAccuracySample(const char* source,
                                const BleCsEstimate& estimate,
                                uint8_t validChannels,
                                uint8_t requestedChannels,
                                uint8_t minValidChannels) {
  const uint8_t confidence =
      estimateConfidencePercent(estimate, validChannels, requestedChannels,
                                minValidChannels);
  const float calibratedDistance =
      BleChannelSoundingRadio::applyCalibrationProfile(
          estimate.distanceMeters, kConnectedCalibrationProfile);
  const float calibratedPhase =
      BleChannelSoundingRadio::applyCalibrationProfile(
          estimate.phaseSlopeDistanceMeters, kConnectedCalibrationProfile);
  BleCsPhysicalDistanceEstimate physical{};
  const bool physicalOk =
      BleChannelSoundingRadio::estimatePhysicalDistance(
          estimate.distanceMeters, kConnectedCalibrationProfile, &physical);

  Serial.print("cs_accuracy_sample source=");
  Serial.print(source);
  Serial.print(" profile=");
  Serial.print(kConnectedPhysicalChannelProfile);
  Serial.print(" profile_channels=");
  Serial.print(kConnectedPhysicalSweepChannelCount);
  Serial.print(" executed_channels=");
  Serial.print(requestedChannels);
  Serial.print(" rtt_enabled=");
  Serial.print(kConnectedEnableRtt ? 1 : 0);
  Serial.print(" requested_channels=");
  Serial.print(requestedChannels);
  Serial.print(" valid_channels=");
  Serial.print(validChannels);
  Serial.print(" used_channels=");
  Serial.print(estimate.usedChannels);
  Serial.print(" total_channels=");
  Serial.print(estimate.totalToneChannels);
  Serial.print(" rtt_channels=");
  Serial.print(estimate.rttChannels);
  Serial.print(" rejected_low=");
  Serial.print(estimate.rejectedLowQualityChannels);
  Serial.print(" rejected_residual=");
  Serial.print(estimate.rejectedResidualChannels);
  printDistanceField(" phase_raw_m=", estimate.phaseSlopeDistanceMeters);
  printDistanceField(" phase_m=", calibratedPhase);
  printDistanceField(" rtt_m=", estimate.rttDistanceMeters);
  printDistanceField(" dist_raw_m=", estimate.distanceMeters);
  printDistanceField(" dist_m=", calibratedDistance);
  Serial.print(" slope=");
  Serial.print(estimate.slopeRadPerHz, 10);
  Serial.print(" residual=");
  Serial.print(estimate.residualVariance, 6);
  Serial.print(" rtt_var=");
  Serial.print(estimate.rttVariance, 6);
  Serial.print(" median_quality=");
  Serial.print(estimate.medianToneQuality, 4);
  Serial.print(" fit_delta_m=");
  Serial.print(estimate.fitDeltaMeters, 4);
  Serial.print(" confidence=");
  Serial.print(confidence);
  Serial.print(" confidence_label=");
  Serial.print(confidenceLabel(confidence));
  Serial.print(" calib_scale=");
  Serial.print(kConnectedCalibrationProfile.scale, 6);
  Serial.print(" calib_offset_m=");
  Serial.print(kConnectedCalibrationProfile.offsetMeters, 4);
  Serial.print(" calibrated_window=");
  Serial.print(physicalOk ? 1 : 0);
  if (physicalOk) {
    printDistanceField(" typical_error_m=", physical.typicalErrorMeters);
    printDistanceField(" conservative_error_m=",
                       physical.conservativeErrorMeters);
    printDistanceField(" lower_m=", physical.lowerBoundMeters);
    printDistanceField(" upper_m=", physical.upperBoundMeters);
  }
  Serial.print("\r\n");

  Serial.print("cs_distance_m=");
  Serial.print(calibratedDistance, 4);
  Serial.print(" raw_m=");
  Serial.print(estimate.distanceMeters, 4);
  Serial.print(" phase_m=");
  Serial.print(calibratedPhase, 4);
  Serial.print(" confidence=");
  Serial.print(confidenceLabel(confidence));
  Serial.print(" confidence_pct=");
  Serial.print(confidence);
  Serial.print(" channels=");
  Serial.print(validChannels);
  Serial.print('/');
  Serial.print(requestedChannels);
  Serial.print(" rtt=");
  Serial.print(kConnectedEnableRtt ? 1 : 0);
  if (physicalOk) {
    Serial.print(" typical_error_m=");
    Serial.print(physical.typicalErrorMeters, 4);
    Serial.print(" range_m=");
    Serial.print(physical.lowerBoundMeters, 4);
    Serial.print("..");
    Serial.print(physical.upperBoundMeters, 4);
  }
  Serial.print("\r\n");
}

static const char* phaseName(BleCsControllerWorkflowPhase phase) {
  return BleCsControllerWorkflow::phaseName(phase);
}

static void updateWorkflowMask() {
  const BleCsControllerWorkflowState& wf = g_csHost.workflowState();
  if (wf.remoteCapabilitiesValid) markBit(&g_workflowMask, kBitRemoteCaps);
  if (wf.defaultSettingsApplied) markBit(&g_workflowMask, kBitDefaults);
  if (wf.configCreated) markBit(&g_workflowMask, kBitConfig);
  if (wf.securityEnabled) markBit(&g_workflowMask, kBitSecurity);
  if (wf.procedureParametersApplied) markBit(&g_workflowMask, kBitProcedureParams);
  if (wf.procedureEnabled) markBit(&g_workflowMask, kBitProcedureEnabled);
  if (g_csHost.ready()) markBit(&g_workflowMask, kBitReady);
}

static bool bridgeComplete() {
  static constexpr uint32_t kExpectedWorkflowMask =
      (1UL << kBitRemoteCaps) |
      (1UL << kBitDefaults) |
      (1UL << kBitConfig) |
      (1UL << kBitSecurity) |
      (1UL << kBitProcedureParams) |
      (1UL << kBitProcedureEnabled) |
      (1UL << kBitReady);
  static constexpr uint32_t kExpectedTxMask =
      (1UL << kTxCsReq) |
      (1UL << kTxSecReq) |
      (1UL << kTxProcReq);
  static constexpr uint32_t kExpectedRxMask =
      (1UL << kRxCsRsp) |
      (1UL << kRxCsCfg) |
      (1UL << kRxSecRsp) |
      (1UL << kRxProcRsp) |
      (1UL << kRxStart) |
      (1UL << kRxAbort);
  return (g_workflowMask & kExpectedWorkflowMask) == kExpectedWorkflowMask &&
         (g_txMask & kExpectedTxMask) == kExpectedTxMask &&
         (g_rxMask & kExpectedRxMask) == kExpectedRxMask;
}

static void printStatus(const char* prefix) {
  const BleCsControllerWorkflowState& wf = g_csHost.workflowState();
  BleChannelSoundingLlControlDebug dbg{};
  g_ble.getChannelSoundingLlControlDebug(&dbg);

  Serial.print(prefix);
  Serial.print(" phase=");
  Serial.print(phaseName(wf.phase));
  Serial.print(" ready=");
  Serial.print(g_csHost.ready() ? 1 : 0);
  Serial.print(" ev=");
  Serial.print(g_linkEvents);
  Serial.print(" txq=");
  Serial.print(g_txQueued);
  Serial.print(" vpr_pdu=");
  Serial.print(g_vprPduQueued);
  Serial.print(" inj=");
  Serial.print(g_peerPdusInjected);
  Serial.print(" direct=");
  Serial.print(g_directCommands);
  Serial.print(" wf=0x");
  Serial.print(g_workflowMask, HEX);
  Serial.print(" tx=0x");
  Serial.print(g_txMask, HEX);
  Serial.print(" rx=0x");
  Serial.print(g_rxMask, HEX);
  Serial.print(" ble_rx=");
  Serial.print(dbg.rxCount);
  Serial.print(" ble_txsent=");
  Serial.print(dbg.txSentCount);
  Serial.print(" stage=");
  Serial.print(g_lastVprStage);
  Serial.print(" status=0x");
  Serial.print(g_lastVprStatus, HEX);
  Serial.print(" local=");
  Serial.print(g_csHost.hostState().localSubeventResults);
  Serial.print(" peer=");
  Serial.print(g_csHost.hostState().peerSubeventResults);
  Serial.print(" proc=");
  Serial.print(g_csHost.sessionState().completedProcedureCounter);
  Serial.print(" est=");
  Serial.print(g_csHost.estimateValid() ? 1 : 0);
  Serial.print(" last_rx=");
  printOpcode(dbg.lastRxOpcode);
  Serial.print(" last_tx=");
  printOpcode(dbg.lastTxOpcode);
  Serial.print("\r\n");
}

static void markTxOpcode(uint8_t opcode) {
  switch (opcode) {
    case kBleCsLlCtrlReq:
      markBit(&g_txMask, kTxCsReq);
      break;
    case kBleCsLlCtrlSecReq:
      markBit(&g_txMask, kTxSecReq);
      break;
    case kBleCsLlCtrlProcReq:
      markBit(&g_txMask, kTxProcReq);
      break;
    default:
      break;
  }
}

static void markRxOpcode(uint8_t opcode) {
  switch (opcode) {
    case kBleCsLlCtrlRsp:
      markBit(&g_rxMask, kRxCsRsp);
      break;
    case kBleCsLlCtrlCfg:
      markBit(&g_rxMask, kRxCsCfg);
      break;
    case kBleCsLlCtrlSecRsp:
      markBit(&g_rxMask, kRxSecRsp);
      break;
    case kBleCsLlCtrlProcRsp:
      markBit(&g_rxMask, kRxProcRsp);
      break;
    case kBleCsLlCtrlStart:
      markBit(&g_rxMask, kRxStart);
      break;
    case kBleCsLlCtrlAbort:
      markBit(&g_rxMask, kRxAbort);
      break;
    default:
      break;
  }
}

static bool beginWorkflowBridge() {
  BleCsControllerVprHost::fillDemoConfig(&g_csConfig);
  g_csConfig.builtInPeerDemo.enabled = false;
  g_csConfig.session.workflow.createConfig.minMainModeSteps = 6U;
  g_csConfig.session.workflow.createConfig.maxMainModeSteps = 6U;
  g_csConfig.session.workflow.createConfig.rttType = 0U;
  g_csConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  g_csConfig.session.workflow.procedureEnable.enable = 1U;

  return g_csHost.resetTransport(true) &&
         g_csHost.loadDefaultTransportImage() &&
         g_csHost.bootTransport() &&
         g_csHost.beginHost(kCsConnHandle, g_csConfig) &&
         !g_csHost.failed();
}

static void handleBridgeServiceResult(
    const BleCsLlControlBridgeServiceResult& result) {
  g_lastVprStage = result.state.currentStage;
  g_lastVprStatus = result.state.status;

  if (result.peerPduConsumed) {
    ++g_peerPdusInjected;
    markRxOpcode(result.rxOpcode);
    Serial.print("VPR inject op=");
    printOpcode(result.rxOpcode);
    Serial.print(" prev=");
    Serial.print(result.peerState.previousStage);
    Serial.print(" stage=");
    Serial.print(result.peerState.currentStage);
    Serial.print("\r\n");
  }

  if (result.directCommandSent) {
    ++g_directCommands;
    g_lastVprStatus = result.directStatus;
  }

  if (result.initiatorPduQueued) {
    ++g_txQueued;
    if (result.initiatorPduSourceVpr) {
      ++g_vprPduQueued;
    }
    markTxOpcode(result.txOpcode);
    Serial.print("queued op=");
    printOpcode(result.txOpcode);
    Serial.print(" src=");
    Serial.print(result.initiatorPduSourceVpr ? "vpr" : "host");
    Serial.print("\r\n");
  }
}

static void failBridge(const char* reason,
                       const BleCsLlControlBridgePollResult* result = nullptr) {
  g_failed = true;
  Serial.print("cs_ll_workflow_bridge=FAIL reason=");
  Serial.print(reason);
  if (result != nullptr) {
    Serial.print(" rx=");
    printOpcode(result->rxOpcode != 0U ? result->rxOpcode : result->service.rxOpcode);
    Serial.print(" stage=");
    Serial.print(result->service.state.currentStage);
    Serial.print(" status=0x");
    Serial.print(result->service.state.status, HEX);
  }
  Serial.print("\r\n");
}

static void printAutoProofWork(const BleCsVprMeasurementWorkItem& work) {
  Serial.print(" work_valid=");
  Serial.print(work.valid ? 1 : 0);
  Serial.print(" work_status=0x");
  Serial.print(work.status, HEX);
  Serial.print(" work_flags=0x");
  Serial.print(work.flags, HEX);
  Serial.print(" work_auto=");
  Serial.print(work.controllerAutoExecuted ? 1 : 0);
  Serial.print(" work_ready=");
  Serial.print(work.ready ? 1 : 0);
  Serial.print(" work_proc=");
  Serial.print(work.procedureCounter);
  Serial.print(" work_sub=");
  Serial.print(work.activeSubeventIndex);
  Serial.print('/');
  Serial.print(work.totalSubevents);
  Serial.print(" work_steps=");
  Serial.print(work.subeventStepCount);
  Serial.print('/');
  Serial.print(work.totalSteps);
  Serial.print(" work_auto_block=0x");
  Serial.print(work.controllerAutoBlockMask, HEX);
  Serial.print(" work_auto_count=");
  Serial.print(work.controllerAutoCount);
  Serial.print(" work_auto_calls=");
  Serial.print(work.controllerAutoServiceCalls);
  Serial.print(" work_auto_due=");
  Serial.print(work.controllerAutoDuePasses);
  Serial.print(" work_auto_key=");
  Serial.print(work.controllerAutoProcedureCounter);
  Serial.print('/');
  Serial.print(work.controllerAutoSubevent);
  Serial.print(" work_auto_status=0x");
  Serial.print(work.controllerAutoStatus, HEX);
  Serial.print(" work_ch=");
  Serial.print(work.stepChannelCount);
  Serial.print(':');
  for (uint8_t i = 0U; i < work.stepChannelCount; ++i) {
    if (i != 0U) {
      Serial.print(',');
    }
    Serial.print(work.stepChannels[i]);
  }
}

static void printAutoProofExec(const char* prefix,
                               const BleCsVprMeasurementExecutionResult& exec) {
  Serial.print(' ');
  Serial.print(prefix);
  Serial.print("_valid=");
  Serial.print(exec.valid ? 1 : 0);
  Serial.print(' ');
  Serial.print(prefix);
  Serial.print("_status=0x");
  Serial.print(exec.status, HEX);
  Serial.print(' ');
  Serial.print(prefix);
  Serial.print("_flags=0x");
  Serial.print(exec.flags, HEX);
  Serial.print(' ');
  Serial.print(prefix);
  Serial.print("_snap=");
  Serial.print(exec.controllerOwnedSnapshot ? 1 : 0);
  Serial.print(' ');
  Serial.print(prefix);
  Serial.print("_accepted=");
  Serial.print(exec.accepted ? 1 : 0);
  Serial.print(' ');
  Serial.print(prefix);
  Serial.print("_count=");
  Serial.print(exec.executeCount);
  Serial.print(' ');
  Serial.print(prefix);
  Serial.print("_token=");
  Serial.print(exec.executionToken);
  Serial.print(' ');
  Serial.print(prefix);
  Serial.print("_proc=");
  Serial.print(exec.procedureCounter);
  Serial.print(' ');
  Serial.print(prefix);
  Serial.print("_sub=");
  Serial.print(exec.activeSubeventIndex);
  Serial.print('/');
  Serial.print(exec.totalSubevents);
}

static void failAutoMeasurementProof(
    const char* reason,
    const BleCsVprMeasurementWorkItem* work = nullptr,
    const BleCsVprMeasurementExecutionResult* exec = nullptr,
    const BleCsVprMeasurementExecutionResult* exec2 = nullptr) {
  g_failed = true;
  g_autoProofPrinted = true;
  Serial.print("cs_vpr_auto_measurement=FAIL reason=");
  Serial.print(reason);
  if (work != nullptr) {
    printAutoProofWork(*work);
  }
  if (exec != nullptr) {
    printAutoProofExec("exec", *exec);
  }
  if (exec2 != nullptr) {
    printAutoProofExec("exec2", *exec2);
  }
  Serial.print("\r\n");
}

static bool printAutoMeasurementProof() {
  if (g_autoProofPrinted) {
    return !g_failed;
  }

  BleCsVprMeasurementWorkItem work{};
  bool workReadOk = false;
  for (uint8_t retry = 0U; retry < kAutoMeasurementProofReadRetries; ++retry) {
    workReadOk = g_csHost.directReadMeasurementWorkItemForTest(&work);
    if (workReadOk && work.valid && work.status == 0U && work.ready &&
        work.controllerAutoExecuted &&
        ((work.flags & kVprWorkFlagControllerAutoExecuted) != 0U)) {
      break;
    }
    delay(kAutoMeasurementProofRetryDelayMs);
  }

  if (!workReadOk) {
    failAutoMeasurementProof("work_read");
    return false;
  }
  if (!work.valid || work.status != 0U || !work.ready) {
    failAutoMeasurementProof("work_not_ready", &work);
    return false;
  }
  if (!work.controllerAutoExecuted ||
      ((work.flags & kVprWorkFlagControllerAutoExecuted) == 0U)) {
    failAutoMeasurementProof("work_not_controller_auto", &work);
    return false;
  }

  BleCsVprMeasurementExecutionResult exec{};
  if (!g_csHost.readMeasurementExecutionSnapshot(&exec)) {
    failAutoMeasurementProof("snapshot_read", &work);
    return false;
  }
  if (!exec.valid || exec.status != 0U || !exec.accepted ||
      !exec.controllerOwnedSnapshot ||
      ((exec.flags & kVprExecFlagControllerOwnedSnapshot) == 0U) ||
      !exec.executionTokenValid || exec.executeCount == 0U ||
      exec.procedureCounter != work.procedureCounter) {
    failAutoMeasurementProof("snapshot_invalid", &work, &exec);
    return false;
  }

  BleCsVprMeasurementExecutionResult exec2{};
  if (!g_csHost.readMeasurementExecutionSnapshot(&exec2)) {
    failAutoMeasurementProof("snapshot_repeat_read", &work, &exec);
    return false;
  }
  const bool snapshotStable =
      exec2.valid && exec2.status == 0U && exec2.accepted &&
      exec2.controllerOwnedSnapshot &&
      ((exec2.flags & kVprExecFlagControllerOwnedSnapshot) != 0U) &&
      exec2.executeCount == exec.executeCount &&
      exec2.executionToken == exec.executionToken &&
      exec2.procedureCounter == exec.procedureCounter &&
      exec2.activeSubeventIndex == exec.activeSubeventIndex;
  if (!snapshotStable) {
    failAutoMeasurementProof("snapshot_not_stable", &work, &exec, &exec2);
    return false;
  }

  g_autoProofPrinted = true;
  Serial.print("cs_vpr_auto_measurement=PASS");
  printAutoProofWork(work);
  printAutoProofExec("exec", exec);
  printAutoProofExec("exec2", exec2);
  Serial.print(" stable=1 no_host_execute=1\r\n");
  return true;
}

static void maybeRunAutoMeasurementProof(
    const BleCsLlControlBridgeServiceResult& result) {
  if (!kAutoMeasurementProofOnly || g_autoProofPrinted ||
      !result.peerPduConsumed || result.rxOpcode != kBleCsLlCtrlStart ||
      result.peerState.currentStage != kVprStageProcedureActive) {
    return;
  }

  (void)printAutoMeasurementProof();
}

static void resetBridgeState() {
  g_ble.clearChannelSoundingLlControlDebug();
  g_csHost.reset();
  g_linkEvents = 0U;
  g_txQueued = 0U;
  g_vprPduQueued = 0U;
  g_peerPdusInjected = 0U;
  g_directCommands = 0U;
  g_workflowMask = 0U;
  g_txMask = 0U;
  g_rxMask = 0U;
  g_lastVprStage = 0xFFU;
  g_lastVprStatus = 0xFFU;
  g_bridgeTracker.reset();
  g_passPrinted = false;
  g_failed = false;
  g_autoProofPrinted = false;
  g_autoProofStartedMs = millis();
  g_connectedPhysicalAttempted = false;
  g_connectedPhysicalOk = false;
  g_connectedPhysicalAttemptCount = 0U;
  g_connectedPhysicalValidChannels = 0U;
  memset(g_connectedPhysicalMeasurements, 0,
         sizeof(g_connectedPhysicalMeasurements));
  memset(g_connectedPhysicalLocalStepData, 0,
         sizeof(g_connectedPhysicalLocalStepData));
  memset(g_connectedPhysicalPeerStepData, 0,
         sizeof(g_connectedPhysicalPeerStepData));
  g_bridgeStarted = beginWorkflowBridge();

  Serial.print("workflow bridge init: ");
  Serial.print(g_bridgeStarted ? "OK" : "FAIL");
  Serial.print(" phase=");
  Serial.print(phaseName(g_csHost.workflowState().phase));
  Serial.print("\r\n");
}

static void printConnectedPhysicalChannelResult(
    const BleCsConnectedMode2ChannelResult& result,
    void*) {
  const BleCsConnectedWindowMeasurement& window = result.window;
  const BleCsChannelMeasurement& measurement = window.measurement;
  const BleCsDfeCaptureInfo& dfeInfo = result.dfeInfo;

  Serial.print("cs_connected_physical snapshot=");
  Serial.print(result.snapshotValid ? 1 : 0);
  Serial.print(" rf=");
  Serial.print(result.rfPathEnabled ? 1 : 0);
  Serial.print(" raw=");
  Serial.print(result.radioStarted ? 1 : 0);
  Serial.print(" fit=");
  Serial.print(window.plan.fits ? 1 : 0);
  Serial.print(" attempted=");
  Serial.print(window.attempted ? 1 : 0);
  Serial.print(" ok=");
  Serial.print(result.channelOk ? 1 : 0);
  Serial.print(" idx=");
  Serial.print(result.order);
  Serial.print(" ch=");
  Serial.print(result.channel);
  Serial.print(" valid=");
  Serial.print(measurement.valid ? 1 : 0);
  Serial.print(" status=");
  Serial.print(measurement.status);
  Serial.print(" local_tone=");
  Serial.print(measurement.localTone.valid ? 1 : 0);
  Serial.print(" peer_tone=");
  Serial.print(measurement.peerTone.valid ? 1 : 0);
  Serial.print(" local_mag=");
  Serial.print(measurement.localTone.magnitude);
  Serial.print(" peer_mag=");
  Serial.print(measurement.peerTone.magnitude);
  Serial.print(" local_i=");
  Serial.print(measurement.localTone.i);
  Serial.print(" local_q=");
  Serial.print(measurement.localTone.q);
  Serial.print(" peer_i=");
  Serial.print(measurement.peerTone.i);
  Serial.print(" peer_q=");
  Serial.print(measurement.peerTone.q);
  Serial.print(" dfe_present=");
  Serial.print(dfeInfo.present ? 1 : 0);
  Serial.print(" dfe_zero=");
  Serial.print(dfeInfo.allZero ? 1 : 0);
  Serial.print(" dfe_amount=");
  Serial.print(dfeInfo.amountBytes);
  Serial.print(" dfe_current=");
  Serial.print(dfeInfo.currentAmountBytes);
  Serial.print(" avail_us=");
  Serial.print(window.plan.availableUs);
  Serial.print(" start_delay_us=");
  Serial.print(window.startDelayUs);
  Serial.print(" elapsed_us=");
  Serial.print(window.elapsedUs);
  Serial.print(" remaining_us=");
  Serial.print(window.remainingUs);
  Serial.print(" ctrl_tx_us=");
  Serial.print(measurement.controlTxUs);
  Serial.print(" probe_gap_us=");
  Serial.print(measurement.controlToProbeGapUs);
  Serial.print(" probe_tx_us=");
  Serial.print(measurement.probeTxUs);
  Serial.print(" report_rx_us=");
  Serial.print(measurement.reportRxUs);
  Serial.print(" reason=");
  Serial.print(result.reason);
  Serial.print("\r\n");

  Serial.print("cs_connected_trigger queued=");
  Serial.print(result.triggerQueued ? 1 : 0);
  Serial.print(" sent=");
  Serial.print(result.triggerSent ? 1 : 0);
  Serial.print(" ce=");
  Serial.print(result.triggerEventCounter);
  Serial.print(" ack=");
  Serial.print(result.triggerAcked ? 1 : 0);
  Serial.print(" ack_ce=");
  Serial.print(result.ackEventCounter);
  Serial.print(" run_after_ce=");
  Serial.print(result.runAfterEventCounter);
  Serial.print(" raw_before_ce=");
  Serial.print(static_cast<uint16_t>(result.runAfterEventCounter + 1U));
  Serial.print(" idx=");
  Serial.print(result.order);
  Serial.print(" ch=");
  Serial.print(result.channel);
  Serial.print("\r\n");
}

static bool runConnectedPhysicalSweep(const BleCsVprMeasurementWorkItem* workItem = nullptr) {
  if (g_connectedPhysicalAttempted) {
    return g_connectedPhysicalValidChannels >= kConnectedPhysicalMinValidChannels;
  }

  g_connectedPhysicalAttempted = true;
  g_connectedPhysicalOk = false;
  g_connectedPhysicalAttemptCount = 0U;
  g_connectedPhysicalValidChannels = 0U;

  BleCsConfig radioConfig;
  radioConfig.txPowerDbm = -8;
  radioConfig.controlChannel = 37U;
  radioConfig.controlToProbeDelayUs = 5000U;
  radioConfig.probeToReportDelayUs = 1000U;
  radioConfig.probeRetries = 1U;
  radioConfig.probeListenWindowUs = 10000U;
  radioConfig.responseListenWindowUs = 9000U;
  radioConfig.maxPayloadLength = 32U;
  radioConfig.minToneMagnitude = 8U;
  radioConfig.enableRtt = kConnectedEnableRtt;
  radioConfig.enableRawDfeCapture = true;

  BleCsConnectedMode2SweepConfig sweepConfig{};
  sweepConfig.channels = kConnectedPhysicalSweepChannels;
  sweepConfig.channelCount = kConnectedPhysicalSweepChannelCount;
  sweepConfig.minValidChannels = kConnectedPhysicalMinValidChannels;
  sweepConfig.configId = 1U;
  sweepConfig.workItem = workItem;
  sweepConfig.numAntennaPaths = 1U;
  sweepConfig.triggerReason = kConnectedPhysicalTriggerReason;
  sweepConfig.ackReason = kConnectedPhysicalAckReason;
  sweepConfig.windowEventOffset = kConnectedPhysicalWindowEventOffset;
  sweepConfig.singleChannelWindowUs = kConnectedCsSingleChannelWindowUs;
  sweepConfig.guardBeforeUs = kConnectedCsGuardBeforeUs;
  sweepConfig.guardAfterUs = kConnectedCsGuardAfterUs;
  sweepConfig.antennaPath = kPhysicalAntennaPath;
  sweepConfig.radioConfig = radioConfig;
  sweepConfig.inOutSequence = &g_physicalSequence;

  BleCsConnectedMode2SweepResult sweepResult{};
  g_connectedPhysicalOk =
      BleCsConnectedMode2SweepRunner::runInitiator(
          g_ble, g_physicalCs, &g_csHost, sweepConfig,
          g_connectedPhysicalMeasurements,
          g_connectedPhysicalLocalStepData,
          sizeof(g_connectedPhysicalLocalStepData),
          g_connectedPhysicalPeerStepData,
          sizeof(g_connectedPhysicalPeerStepData),
          &sweepResult,
          printConnectedPhysicalChannelResult,
          nullptr);
  const BleCsControllerVprHostState& vprState = g_csHost.vprState();
  g_connectedPhysicalAttemptCount = sweepResult.attempts;
  g_connectedPhysicalValidChannels = sweepResult.validChannels;
  Serial.print("cs_connected_sweep=");
  Serial.print(g_connectedPhysicalOk ? "PASS" : "FAIL");
  Serial.print(" attempts=");
  Serial.print(g_connectedPhysicalAttemptCount);
  Serial.print(" valid_channels=");
  Serial.print(g_connectedPhysicalValidChannels);
  Serial.print(" min_valid=");
  Serial.print(kConnectedPhysicalMinValidChannels);
  Serial.print(" requested_channels=");
  Serial.print(sweepResult.sweepChannelCount);
  Serial.print(" raw_est=");
  Serial.print(sweepResult.rawEstimateValid ? 1 : 0);
  Serial.print(" used=");
  Serial.print(sweepResult.rawEstimate.usedChannels);
  Serial.print('/');
  Serial.print(sweepResult.rawEstimate.totalToneChannels);
  printDistanceField(" raw_m=", sweepResult.rawEstimate.phaseSlopeDistanceMeters);
  Serial.print(" residual=");
  Serial.print(sweepResult.rawEstimate.residualVariance, 6);
  Serial.print(" host_est=");
  Serial.print(sweepResult.hostEstimateValid ? 1 : 0);
  Serial.print(" ctrl_ing=");
  Serial.print(sweepResult.hostControllerResultIngress ? 1 : 0);
  Serial.print(" ctrl_evt_delta=");
  Serial.print(sweepResult.hostControllerEventPacketDelta);
  Serial.print(" local_pkt_delta=");
  Serial.print(sweepResult.hostLocalResultPacketDelta);
  Serial.print(" peer_pkt_delta=");
  Serial.print(sweepResult.hostPeerResultPacketDelta);
  Serial.print(" peer_marker_delta=");
  Serial.print(sweepResult.hostPeerResultMarkerDelta);
  Serial.print(" work_applied=");
  Serial.print(sweepResult.workItemApplied ? 1 : 0);
  Serial.print(" work_ch_used=");
  Serial.print(sweepResult.workChannelsUsed ? 1 : 0);
  Serial.print(" work_auto=");
  Serial.print(sweepResult.workAutoExecuted ? 1 : 0);
  Serial.print(" work_auto_block=0x");
  Serial.print(sweepResult.workAutoBlockMask, HEX);
  Serial.print(" work_auto_count=");
  Serial.print(sweepResult.workAutoCount);
  Serial.print(" work_auto_calls=");
  Serial.print(sweepResult.workAutoServiceCalls);
  Serial.print(" work_auto_due=");
  Serial.print(sweepResult.workAutoDuePasses);
  Serial.print(" work_auto_proc=");
  Serial.print(sweepResult.workAutoProcedureCounter);
  Serial.print(" work_auto_sub=");
  Serial.print(sweepResult.workAutoSubevent);
  Serial.print(" work_auto_status=0x");
  Serial.print(sweepResult.workAutoStatus, HEX);
  Serial.print(" work_exec_snap=");
  Serial.print(sweepResult.workControllerOwnedSnapshot ? 1 : 0);
  Serial.print(" work_exec_cmd=");
  Serial.print(sweepResult.workExecuteCommandUsed ? 1 : 0);
  Serial.print(" work_exec=");
  Serial.print(sweepResult.workExecuteAttempted ? (sweepResult.workExecuteOk ? 1 : 0) : 2);
  Serial.print(" work_exec_mismatch=0x");
  Serial.print(sweepResult.workExecuteMismatchMask, HEX);
  Serial.print(" work_exec_status=0x");
  Serial.print(sweepResult.workExecutionStatus, HEX);
  Serial.print(" work_exec_flags=0x");
  Serial.print(sweepResult.workExecutionFlags, HEX);
  Serial.print(" work_exec_ch=");
  Serial.print(sweepResult.workExecutedChannelCount);
  Serial.print(" work_tok=");
  Serial.print(sweepResult.workExecuteTokenOk ? 1 : 0);
  Serial.print(" work_tok32=0x");
  Serial.print(sweepResult.workExecutionToken, HEX);
  Serial.print(" work_rf=");
  Serial.print(sweepResult.workRfDescriptorOk ? 1 : 0);
  Serial.print(" work_rf32=0x");
  Serial.print(sweepResult.workRfDescriptorToken, HEX);
  Serial.print(" work_rf_hw=");
  Serial.print(sweepResult.workRfHardwareOk ? 1 : 0);
  Serial.print(" work_rf_hw32=0x");
  Serial.print(sweepResult.workRfHardwareToken, HEX);
  Serial.print(" work_rf_state=");
  Serial.print(sweepResult.workRfHardwareState);
  Serial.print(" work_rf_mode=");
  Serial.print(sweepResult.workRfHardwareMode);
  Serial.print(" work_rf_freq=");
  Serial.print(sweepResult.workRfHardwareFrequency);
  Serial.print(" work_rf_prim=");
  Serial.print(sweepResult.workRfPrimitiveOk ? 1 : 0);
  Serial.print(" work_rf_prim32=0x");
  Serial.print(sweepResult.workRfPrimitiveToken, HEX);
  Serial.print(" work_rf_prim_status=");
  Serial.print(sweepResult.workRfPrimitiveStatus);
  Serial.print(" work_rf_prim_flags=0x");
  Serial.print(sweepResult.workRfPrimitiveFlags, HEX);
  Serial.print(" work_rf_prim_before=");
  Serial.print(sweepResult.workRfPrimitiveStateBefore);
  Serial.print(" work_rf_prim_pll=");
  Serial.print(sweepResult.workRfPrimitivePllWaitLoops);
  Serial.print(" work_rf_prim_disable=");
  Serial.print(sweepResult.workRfPrimitiveDisableWaitLoops);
  Serial.print(" work_rf_prim_after=");
  Serial.print(sweepResult.workRfPrimitiveStateAfter);
  Serial.print(" work_rf_retune=");
  Serial.print(sweepResult.workRfRetuneOk ? 1 : 0);
  Serial.print(" work_rf_retune32=0x");
  Serial.print(sweepResult.workRfRetuneToken, HEX);
  Serial.print(" work_rf_retune_status=");
  Serial.print(sweepResult.workRfRetuneStatus);
  Serial.print(" work_rf_retune_flags=0x");
  Serial.print(sweepResult.workRfRetuneFlags, HEX);
  Serial.print(" work_rf_retune_ch=");
  Serial.print(sweepResult.workRfRetuneChannel);
  Serial.print(" work_rf_retune_freq=");
  Serial.print(sweepResult.workRfRetuneTargetFrequency);
  Serial.print(" work_rf_retune_freq_after=");
  Serial.print(sweepResult.workRfRetuneObservedFrequency);
  Serial.print(" work_rf_retune_white=0x");
  Serial.print(sweepResult.workRfRetuneTargetDatawhite, HEX);
  Serial.print(" work_rf_retune_white_after=0x");
  Serial.print(sweepResult.workRfRetuneObservedDatawhite, HEX);
  Serial.print(" work_rf_rx=");
  Serial.print(sweepResult.workRfRxPrimitiveOk ? 1 : 0);
  Serial.print(" work_rf_rx32=0x");
  Serial.print(sweepResult.workRfRxPrimitiveToken, HEX);
  Serial.print(" work_rf_rx_status=");
  Serial.print(sweepResult.workRfRxPrimitiveStatus);
  Serial.print(" work_rf_rx_flags=0x");
  Serial.print(sweepResult.workRfRxPrimitiveFlags, HEX);
  Serial.print(" work_rf_rx_before=");
  Serial.print(sweepResult.workRfRxPrimitiveStateBefore);
  Serial.print(" work_rf_rx_ready=");
  Serial.print(sweepResult.workRfRxPrimitiveRxReadyWaitLoops);
  Serial.print(" work_rf_rx_disable=");
  Serial.print(sweepResult.workRfRxPrimitiveDisableWaitLoops);
  Serial.print(" work_rf_rx_after=");
  Serial.print(sweepResult.workRfRxPrimitiveStateAfter);
  Serial.print(" work_rf_pkt=");
  Serial.print(sweepResult.workRfPacketConfigOk ? 1 : 0);
  Serial.print(" work_rf_pkt32=0x");
  Serial.print(sweepResult.workRfPacketConfigToken, HEX);
  Serial.print(" work_rf_pkt_status=");
  Serial.print(sweepResult.workRfPacketConfigStatus);
  Serial.print(" work_rf_pkt_flags=0x");
  Serial.print(sweepResult.workRfPacketConfigFlags, HEX);
  Serial.print(" work_rf_pkt_max=");
  Serial.print(sweepResult.workRfPacketConfigMaxPayload);
  Serial.print(" work_rf_pkt_pcnf0=0x");
  Serial.print(sweepResult.workRfPacketConfigPcnf0, HEX);
  Serial.print(" work_rf_pkt_pcnf1=0x");
  Serial.print(sweepResult.workRfPacketConfigPcnf1, HEX);
  Serial.print(" work_rf_pkt_s0=0x");
  Serial.print(sweepResult.workRfPacketS0, HEX);
  Serial.print(" work_rf_pkt_cte=0x");
  Serial.print(sweepResult.workRfPacketCteInfo, HEX);
  Serial.print(" work_rf_pkt_len=");
  Serial.print(sweepResult.workRfPacketPayloadLen);
  Serial.print(" work_rf_pkt_seq=");
  Serial.print(sweepResult.workRfPacketSequence);
  Serial.print(" work_rf_pkt_ch=");
  Serial.print(sweepResult.workRfPacketChannel);
  Serial.print(" work_rf_pkt_ctrl_us=");
  Serial.print(sweepResult.workRfPacketControlToProbeDelayUs);
  Serial.print(" work_rf_pkt_listen_us=");
  Serial.print(sweepResult.workRfPacketResponseListenWindowUs);
  Serial.print(" work_rf_buf=");
  Serial.print(sweepResult.workRfPacketBufferOk ? 1 : 0);
  Serial.print(" work_rf_timed=");
  Serial.print(sweepResult.workRfTimedMode2Ok ? 1 : 0);
  Serial.print(" work_rf_timed32=0x");
  Serial.print(sweepResult.workRfTimedMode2Token, HEX);
  Serial.print(" work_rf_timed_status=");
  Serial.print(sweepResult.workRfTimedMode2Status);
  Serial.print(" work_rf_timed_flags=0x");
  Serial.print(sweepResult.workRfTimedMode2Flags, HEX);
  Serial.print(" work_rf_timed_ch=");
  Serial.print(sweepResult.workRfTimedMode2Channel);
  Serial.print(" work_rf_timed_tx=");
  Serial.print(sweepResult.workRfTimedMode2TxWaitLoops);
  Serial.print(" work_rf_timed_gap=");
  Serial.print(sweepResult.workRfTimedMode2GapWaitLoops);
  Serial.print(" work_rf_timed_rxready=");
  Serial.print(sweepResult.workRfTimedMode2RxReadyWaitLoops);
  Serial.print(" work_rf_timed_listen=");
  Serial.print(sweepResult.workRfTimedMode2ListenWaitLoops);
  Serial.print(" work_rf_timed_disable=");
  Serial.print(sweepResult.workRfTimedMode2DisableWaitLoops);
  Serial.print(" work_rf_timed_after=");
  Serial.print(sweepResult.workRfTimedMode2StateAfter);
  Serial.print(" work_rf_timing=");
  Serial.print(sweepResult.workRfTimingOwnerOk ? 1 : 0);
  Serial.print(" work_rf_timing32=0x");
  Serial.print(sweepResult.workRfTimingOwnerToken, HEX);
  Serial.print(" work_rf_timing_status=");
  Serial.print(sweepResult.workRfTimingOwnerStatus);
  Serial.print(" work_rf_timing_flags=0x");
  Serial.print(sweepResult.workRfTimingOwnerFlags, HEX);
  Serial.print(" work_rf_timing_sub=");
  Serial.print(sweepResult.workRfTimingOwnerSubevent);
  Serial.print(" work_rf_timing_hb=");
  Serial.print(sweepResult.workRfTimingOwnerHeartbeat);
  Serial.print(" work_rf_timing_next_proc=");
  Serial.print(sweepResult.workRfTimingOwnerNextProcedureHeartbeat);
  Serial.print(" work_rf_timing_next_sub=");
  Serial.print(sweepResult.workRfTimingOwnerNextSubeventHeartbeat);
  Serial.print(" work_rf_timing_proc_ticks=");
  Serial.print(sweepResult.workRfTimingOwnerProcedureIntervalTicks);
  Serial.print(" work_rf_timing_sub_ticks=");
  Serial.print(sweepResult.workRfTimingOwnerSubeventDelayTicks);
  Serial.print(" work_rf_timing_peer_gap=");
  Serial.print(sweepResult.workRfTimingOwnerPeerGapTicks);
  Serial.print(" work_rf_timing_sel=");
  Serial.print(sweepResult.workRfTimingOwnerIntervalSelector);
  Serial.print(" work_tone_snap=");
  Serial.print(sweepResult.workToneSnapshotOk ? 1 : 0);
  Serial.print(" work_tone_snap32=0x");
  Serial.print(sweepResult.workToneSnapshotToken, HEX);
  Serial.print(" work_tone_snap_status=");
  Serial.print(sweepResult.workToneSnapshotStatus);
  Serial.print(" work_tone_snap_flags=0x");
  Serial.print(sweepResult.workToneSnapshotFlags, HEX);
  Serial.print(" work_tone_pct16=0x");
  Serial.print(sweepResult.workToneSnapshotPct16, HEX);
  Serial.print(" work_tone_magphase=0x");
  Serial.print(sweepResult.workToneSnapshotMagPhase, HEX);
  Serial.print(" work_tone_magstd=0x");
  Serial.print(sweepResult.workToneSnapshotMagStd, HEX);
  Serial.print(" work_tone_freq=");
  Serial.print(sweepResult.workToneSnapshotFrequency);
  Serial.print(" work_tone_state=");
  Serial.print(sweepResult.workToneSnapshotState);
  Serial.print(" work_tone_event=");
  Serial.print(sweepResult.workToneSnapshotCstonesEndEvent);
  Serial.print(" work_tone_timed=");
  Serial.print(sweepResult.workToneTimedMode2Ok ? 1 : 0);
  Serial.print(" work_tone_timed32=0x");
  Serial.print(sweepResult.workToneTimedMode2Token, HEX);
  Serial.print(" work_tone_timed_status=");
  Serial.print(sweepResult.workToneTimedMode2Status);
  Serial.print(" work_tone_timed_flags=0x");
  Serial.print(sweepResult.workToneTimedMode2Flags, HEX);
  Serial.print(" work_tone_timed_ch=");
  Serial.print(sweepResult.workToneTimedMode2Channel);
  Serial.print(" work_tone_timed_type=0x");
  Serial.print(sweepResult.workToneTimedMode2PacketType, HEX);
  Serial.print(" work_tone_timed_pkt_ch=");
  Serial.print(sweepResult.workToneTimedMode2PacketChannel);
  Serial.print(" work_tone_timed_evt=0x");
  Serial.print(sweepResult.workToneTimedMode2EventMask, HEX);
  Serial.print(" work_result_timed=");
  Serial.print(sweepResult.workResultTimedMode2Ok ? 1 : 0);
  Serial.print(" work_result_timed_local=");
  Serial.print(sweepResult.workResultTimedMode2LocalOk ? 1 : 0);
  Serial.print(" work_result_timed_peer=");
  Serial.print(sweepResult.workResultTimedMode2PeerOk ? 1 : 0);
  Serial.print(" work_result_timed_ch=");
  Serial.print(sweepResult.workResultTimedMode2Channel);
  Serial.print(" work_result_timed_all=");
  Serial.print(sweepResult.workResultTimedMode2AllChannelsOk ? 1 : 0);
  Serial.print(" work_result_timed_matches=");
  Serial.print(sweepResult.workResultTimedMode2LocalMatches);
  Serial.print("/");
  Serial.print(sweepResult.workResultTimedMode2PeerMatches);
  Serial.print("/");
  Serial.print(sweepResult.workResultTimedMode2RequiredChannels);
  Serial.print(" work_timed_obs=");
  Serial.print(sweepResult.workRfTimedMode2ObservedCount);
  Serial.print(":");
  for (uint8_t i = 0U; i < sweepResult.workRfTimedMode2ObservedCount &&
                      i < sizeof(sweepResult.workRfTimedMode2ObservedChannels);
       ++i) {
    if (i != 0U) {
      Serial.print(",");
    }
    Serial.print(sweepResult.workRfTimedMode2ObservedChannels[i]);
  }
  Serial.print(" work_comp_est=");
  Serial.print(sweepResult.workCompletedResultEstimateValid ? 1 : 0);
  Serial.print(" work_comp_mask=0x");
  Serial.print(sweepResult.workCompletedResultMismatchMask, HEX);
  Serial.print(" work_comp_cfg=");
  Serial.print(sweepResult.workCompletedResultConfigId);
  Serial.print(" work_comp_proc=");
  Serial.print(sweepResult.workCompletedResultProcedureCounter);
  Serial.print(" work_comp_steps=");
  Serial.print(sweepResult.workCompletedResultLocalSteps);
  Serial.print("/");
  Serial.print(sweepResult.workCompletedResultPeerSteps);
  Serial.print(" work_drain_pkts=");
  Serial.print(sweepResult.workDirectDrainPackets);
  Serial.print(" work_drain_cons=");
  Serial.print(sweepResult.workDirectDrainConsumed);
  Serial.print(" work_drain_rej=");
  Serial.print(sweepResult.workDirectDrainRejected);
  Serial.print(" work_drain_readfail=");
  Serial.print(sweepResult.workDirectDrainReadFailures);
  Serial.print(" work_drain_len=");
  Serial.print(sweepResult.workDirectDrainLastLen);
  Serial.print(" work_drain_evt=0x");
  Serial.print(sweepResult.workDirectDrainLastEvent, HEX);
  Serial.print(" work_drain_sub=0x");
  Serial.print(sweepResult.workDirectDrainLastSubevent, HEX);
  Serial.print(" work_drain_vendor=0x");
  Serial.print(sweepResult.workDirectDrainLastVendor, HEX);
  Serial.print(" work_drain_rej_len=");
  Serial.print(sweepResult.workDirectDrainFirstRejectedLen);
  Serial.print(" work_drain_rej_evt=0x");
  Serial.print(sweepResult.workDirectDrainFirstRejectedEvent, HEX);
  Serial.print(" work_drain_rej_sub=0x");
  Serial.print(sweepResult.workDirectDrainFirstRejectedSubevent, HEX);
  Serial.print(" work_drain_rej_conn=0x");
  Serial.print(sweepResult.workDirectDrainFirstRejectedConnHandle, HEX);
  Serial.print(" work_drain_rej_cfg=");
  Serial.print(sweepResult.workDirectDrainFirstRejectedConfigId);
  Serial.print(" work_drain_rej_proc=");
  Serial.print(sweepResult.workDirectDrainFirstRejectedProcedureCounter);
  Serial.print(" work_drain_rej_steps=");
  Serial.print(sweepResult.workDirectDrainFirstRejectedSteps);
  Serial.print(" work_drain_rej_done=");
  Serial.print(sweepResult.workDirectDrainFirstRejectedProcedureDone);
  Serial.print("/");
  Serial.print(sweepResult.workDirectDrainFirstRejectedSubeventDone);
  Serial.print(" work_drain_res_rej_len=");
  Serial.print(sweepResult.workDirectDrainFirstRejectedResultLen);
  Serial.print(" work_drain_res_rej_sub=0x");
  Serial.print(sweepResult.workDirectDrainFirstRejectedResultSubevent, HEX);
  Serial.print(" work_drain_res_rej_conn=0x");
  Serial.print(sweepResult.workDirectDrainFirstRejectedResultConnHandle, HEX);
  Serial.print(" work_drain_res_rej_cfg=");
  Serial.print(sweepResult.workDirectDrainFirstRejectedResultConfigId);
  Serial.print(" work_drain_res_rej_proc=");
  Serial.print(sweepResult.workDirectDrainFirstRejectedResultProcedureCounter);
  Serial.print(" work_drain_res_rej_steps=");
  Serial.print(sweepResult.workDirectDrainFirstRejectedResultSteps);
  Serial.print(" work_drain_res_rej_done=");
  Serial.print(sweepResult.workDirectDrainFirstRejectedResultProcedureDone);
  Serial.print("/");
  Serial.print(sweepResult.workDirectDrainFirstRejectedResultSubeventDone);
  Serial.print(" vpr_result_reason=");
  Serial.print(vprState.linkResultPublishReason);
  Serial.print(" vpr_result_pending=");
  Serial.print(vprState.linkResultPendingStage);
  Serial.print(" vpr_result_pub_stage=");
  Serial.print(vprState.linkResultPublishedStage);
  Serial.print(" vpr_result_active=");
  Serial.print(vprState.linkMeasurementExecuteResultActive ? 1 : 0);
  Serial.print(" work_rf_phy=");
  Serial.print(sweepResult.workRfPhy);
  Serial.print(" work_rf_tx=");
  Serial.print(sweepResult.workRfTxPowerDelta);
  Serial.print(" work_rf_max=");
  Serial.print(sweepResult.workRfMaxSubeventLen);
  Serial.print(" work_cfg=");
  Serial.print(sweepResult.workConfigId);
  Serial.print(" work_proc=");
  Serial.print(sweepResult.workProcedureCounter);
  Serial.print(" work_sub=");
  Serial.print(sweepResult.workSubeventIndex);
  Serial.print('/');
  Serial.print(sweepResult.workSubeventCount);
  Serial.print(" work_plan=");
  Serial.print(sweepResult.workSubeventStepCount);
  Serial.print('/');
  Serial.print(sweepResult.workTotalSteps);
  Serial.print(" work_ch=");
  Serial.print(sweepResult.workStepChannelCount);
  Serial.print(':');
  for (uint8_t i = 0U; i < sweepResult.workStepChannelCount; ++i) {
    if (i != 0U) {
      Serial.print(',');
    }
    Serial.print(sweepResult.workStepChannels[i]);
  }
  Serial.print(" host_cfg=");
  Serial.print(sweepResult.hostConfigId);
  Serial.print(" host_proc=");
  Serial.print(sweepResult.hostProcedureCounter);
  Serial.print(" host_steps=");
  Serial.print(sweepResult.hostLocalSteps);
  Serial.print('/');
  Serial.print(sweepResult.hostPeerSteps);
  printDistanceField(" host_m=",
                     g_csHost.sessionState().estimate.phaseSlopeDistanceMeters);
  Serial.print("\r\n");

  const BleCsEstimate& connectedEstimate =
      sweepResult.hostEstimateValid ? g_csHost.sessionState().estimate
                                    : sweepResult.rawEstimate;
  if (connectedEstimate.valid) {
    printAccuracySample("connected", connectedEstimate,
                        sweepResult.validChannels,
                        sweepResult.sweepChannelCount,
                        kConnectedPhysicalMinValidChannels);
  }
  return g_connectedPhysicalOk;
}

static bool beginPhysicalFollowup() {
  g_physicalStarted = true;
  g_physicalReady = false;
  g_physicalDone = false;
  g_physicalFailed = false;
  g_physicalSequence = 0U;
  g_physicalSweepCount = 0U;
  g_physicalValidChannels = 0U;
  memset(g_physicalMeasurements, 0, sizeof(g_physicalMeasurements));

  Serial.print("physical follow-up: disconnecting BLE link\r\n");
  const bool disconnected = g_ble.disconnect(900000UL);
  g_ble.end();
  delay(1200);

  BleCsConfig config;
  config.txPowerDbm = -8;
  config.controlChannel = 37U;
  config.controlToProbeDelayUs = 2400U;
  config.probeToReportDelayUs = 1200U;
  config.probeRetries = 4U;
  config.probeListenWindowUs = 8000U;
  config.responseListenWindowUs = 12000U;
  config.maxPayloadLength = 32U;
  config.minToneMagnitude = 16U;
  config.enableRtt = kConnectedEnableRtt;
  config.enableRawDfeCapture = true;

  const bool rfOk = BoardControl::enableRfPath(kPhysicalAntennaPath);
  g_physicalReady = rfOk && g_physicalCs.begin(config);
  Serial.print("physical follow-up init: disconnect=");
  Serial.print(disconnected ? 1 : 0);
  Serial.print(" rf=");
  Serial.print(rfOk ? 1 : 0);
  Serial.print(" raw=");
  Serial.print(g_physicalReady ? 1 : 0);
  Serial.print("\r\n");
  if (!g_physicalReady) {
    g_physicalFailed = true;
  }
  return g_physicalReady;
}

static void printConnectedWindowPlan() {
  BleConnectionTimingSnapshot snapshot{};
  BleCsConnectedWindowPlan singlePlan{};
  BleCsConnectedWindowPlan sweepPlan{};
  const bool snapshotOk = g_ble.getConnectionTimingSnapshot(&snapshot);
  const bool singleFits =
      snapshotOk && BleChannelSoundingRadio::planConnectedWindow(
                        snapshot, kConnectedCsSingleChannelWindowUs,
                        kConnectedCsGuardBeforeUs, kConnectedCsGuardAfterUs,
                        &singlePlan);
  const bool sweepFits =
      snapshotOk && BleChannelSoundingRadio::planConnectedWindow(
                        snapshot, kConnectedCsFullSweepWindowUs,
                        kConnectedCsGuardBeforeUs, kConnectedCsGuardAfterUs,
                        &sweepPlan);

  Serial.print("cs_connected_window snapshot=");
  Serial.print(snapshotOk ? 1 : 0);
  Serial.print(" role=");
  Serial.print(static_cast<uint8_t>(snapshot.role));
  Serial.print(" next_ce=");
  Serial.print(snapshot.nextEventCounter);
  Serial.print(" interval_us=");
  Serial.print(snapshot.intervalUs);
  Serial.print(" until_next_us=");
  Serial.print(snapshot.timeUntilNextEventUs);
  Serial.print(" single_fit=");
  Serial.print(singleFits ? 1 : 0);
  Serial.print(" single_avail_us=");
  Serial.print(singlePlan.availableUs);
  Serial.print(" single_reason=");
  Serial.print(singlePlan.reason);
  Serial.print(" sweep_fit=");
  Serial.print(sweepFits ? 1 : 0);
  Serial.print(" sweep_avail_us=");
  Serial.print(sweepPlan.availableUs);
  Serial.print(" sweep_reason=");
  Serial.print(sweepPlan.reason);
  Serial.print("\r\n");
}

static void runPhysicalFollowup() {
  if (g_physicalDone || g_physicalFailed) {
    delay(100);
    return;
  }
  if (!g_physicalReady) {
    if (!beginPhysicalFollowup()) {
      Serial.print("cs_ll_physical_followup=FAIL reason=init\r\n");
    }
    return;
  }

  uint8_t validChannels = 0U;
  (void)g_physicalCs.measureMode2Sweep(kPhysicalChannelCount,
                                       &g_physicalSequence,
                                       g_physicalMeasurements,
                                       &validChannels);

  ++g_physicalSweepCount;
  g_physicalValidChannels = validChannels;

  BleCsEstimate rawEstimate{};
  const bool rawEstimateValid =
      BleChannelSoundingRadio::estimateDistancePhaseSlope(
          g_physicalMeasurements, kPhysicalChannelCount, &rawEstimate);

  BleCsSubeventResultHeader header{};
  header.connHandle = kCsConnHandle;
  header.configId = 1U;
  header.procedureCounter =
      static_cast<uint16_t>(g_csHost.sessionState().completedProcedureCounter + 1U);
  if (header.procedureCounter == 0U) {
    header.procedureCounter = 1U;
  }
  header.numAntennaPaths = 1U;

  const bool hostOk =
      g_csHost.consumeMode2ResultsFromMeasurements(
          g_physicalMeasurements, kPhysicalChannelCount, header,
          g_physicalLocalStepData, sizeof(g_physicalLocalStepData),
          g_physicalPeerStepData, sizeof(g_physicalPeerStepData)) &&
      g_csHost.estimateValid();
  const uint16_t localSteps =
      hostOk ? g_csHost.completedLocalResult().header.numStepsReported : 0U;
  const uint16_t peerSteps =
      hostOk ? g_csHost.completedPeerResult().header.numStepsReported : 0U;

  if (validChannels >= kPhysicalMinValidChannels && rawEstimateValid && hostOk) {
    g_physicalDone = true;
    Serial.print("cs_ll_physical_followup=PASS sweeps=");
    Serial.print(g_physicalSweepCount);
    Serial.print(" valid_channels=");
    Serial.print(validChannels);
    Serial.print(" raw_est=");
    Serial.print(rawEstimateValid ? 1 : 0);
    printDistanceField(" raw_m=", rawEstimate.phaseSlopeDistanceMeters);
    Serial.print(" host_est=");
    Serial.print(g_csHost.estimateValid() ? 1 : 0);
    Serial.print(" host_steps=");
    Serial.print(localSteps);
    Serial.print('/');
    Serial.print(peerSteps);
    printDistanceField(" host_m=",
                       g_csHost.sessionState().estimate.phaseSlopeDistanceMeters);
    Serial.print(" proc=");
    Serial.print(g_csHost.sessionState().completedProcedureCounter);
    Serial.print("\r\n");
    printAccuracySample("followup", rawEstimate, validChannels,
                        kPhysicalChannelCount, kPhysicalMinValidChannels);
    return;
  }

  Serial.print("physical debug sweeps=");
  Serial.print(g_physicalSweepCount);
  Serial.print(" valid_channels=");
  Serial.print(validChannels);
  Serial.print(" raw_est=");
  Serial.print(rawEstimateValid ? 1 : 0);
  Serial.print(" host_est=");
  Serial.print(hostOk ? 1 : 0);
  Serial.print("\r\n");

  if (g_physicalSweepCount >= kPhysicalMaxSweeps) {
    g_physicalFailed = true;
    Serial.print("cs_ll_physical_followup=FAIL reason=sweep valid_channels=");
    Serial.print(validChannels);
    Serial.print(" raw_est=");
    Serial.print(rawEstimateValid ? 1 : 0);
    Serial.print(" host_est=");
    Serial.print(hostOk ? 1 : 0);
    Serial.print("\r\n");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleChannelSoundingLlControlWorkflowCentral start\r\n");
  Serial.print("Arduino CS serial test: upload ");
  Serial.print("BleChannelSoundingLlControlPeripheral to the second nRF54L15 ");
  Serial.print("board, then watch this monitor for cs_distance_m lines.\r\n");
  Serial.print("cs_serial_initiator=READY baud=115200 peer=");
  printAddress(kPeripheralAddress);
  Serial.print("\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
}

void loop() {
  if (kAutoMeasurementProofOnly && g_autoProofPrinted) {
    delay(100);
    return;
  }

  if (g_physicalStarted) {
    runPhysicalFollowup();
    return;
  }

  if (!g_ble.isConnected()) {
    if (g_wasConnected) {
      g_wasConnected = false;
      Serial.print("disconnected\r\n");
      printStatus("final");
      Gpio::write(kPinUserLed, true);
    }

    const bool started =
        g_ble.initiateConnection(kPeripheralAddress, true, 24U, 200U, 9U, 300000UL);
    ++g_connectAttempts;
    if (started) {
      Serial.print("connect attempt: sent\r\n");
    }

    const uint32_t now = millis();
    if ((now - g_lastStatusMs) >= kStatusIntervalMs) {
      g_lastStatusMs = now;
      Serial.print("connecting attempts=");
      Serial.print(g_connectAttempts);
      Serial.print("\r\n");
    }
    delay(20);
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    Serial.print("connected\r\n");
    Gpio::write(kPinUserLed, false);
    resetBridgeState();
  }

  if (!g_bridgeStarted) {
    if (!g_failed) {
      failBridge("bridge_init");
    }
    delay(100);
    return;
  }

  BleCsLlControlBridgePollResult poll{};
  if (!g_csHost.pumpInitiatorLlControlWorkflowBridge(
          g_ble, &g_bridgeTracker, &poll, 1U, 450000UL)) {
    failBridge("poll", &poll);
  } else {
    updateWorkflowMask();
    if (poll.eventStarted) {
      ++g_linkEvents;
      if (poll.csLlControlReceived) {
        Serial.print("CS LL RX ce=");
        Serial.print(poll.eventCounter);
        Serial.print(" op=");
        printOpcode(poll.rxOpcode);
        Serial.print(" len=");
        Serial.print(poll.rxPayloadLength);
        Serial.print("\r\n");
      }
    }

    if (poll.preServiceCalled) {
      handleBridgeServiceResult(poll.preService);
      maybeRunAutoMeasurementProof(poll.preService);
    }
    if (poll.eventServiceCalled) {
      handleBridgeServiceResult(poll.eventService);
      maybeRunAutoMeasurementProof(poll.eventService);
    } else if (poll.serviceCalled && !poll.preServiceCalled) {
      handleBridgeServiceResult(poll.service);
      maybeRunAutoMeasurementProof(poll.service);
    }
    updateWorkflowMask();

    if (kAutoMeasurementProofOnly) {
      if (!g_autoProofPrinted &&
          (millis() - g_autoProofStartedMs) >= kAutoMeasurementProofTimeoutMs) {
        BleCsVprMeasurementWorkItem work{};
        if (g_csHost.directReadMeasurementWorkItemForTest(&work)) {
          failAutoMeasurementProof("timeout", &work);
        } else {
          failAutoMeasurementProof("timeout_work_read");
        }
      }
      delay(1);
      return;
    }

    if (!g_passPrinted &&
        g_bridgeTracker.workflowComplete() &&
        g_bridgeTracker.txComplete() &&
        g_bridgeTracker.rxComplete()) {
      g_passPrinted = true;
      BleCsVprSchedulerState scheduler{};
      const bool schedulerOk =
          g_csHost.directReadSchedulerStateForTest(&scheduler) &&
          scheduler.valid && scheduler.status == 0U;
      BleCsVprMeasurementWorkItem work{};
      const bool workOk =
          g_csHost.directReadMeasurementWorkItemForTest(&work) &&
          work.valid && work.status == 0U && work.ready;
      BleCsVprSecurityMaterialState security{};
      const bool securityOk =
          g_csHost.directReadSecurityMaterialForTest(&security) &&
          security.valid && security.status == 0U &&
          security.materialValid && security.controllerOwned &&
          security.boundToConfig && security.materialValidRaw != 0U &&
          security.connHandle == kCsConnHandle &&
          security.configId == g_csConfig.session.workflow.createConfig.configId &&
          security.drbgNonce != 0U && security.materialToken != 0U &&
          security.sessionCounter != 0U;
      (void)runConnectedPhysicalSweep(workOk ? &work : nullptr);
      BleCsLlControlBridgePollResult trackerRefresh{};
      g_bridgeTracker.update(g_csHost, trackerRefresh);
      Serial.print("cs_ll_workflow_bridge=PASS wf=0x");
      Serial.print(g_bridgeTracker.workflowMask, HEX);
      Serial.print(" tx=0x");
      Serial.print(g_bridgeTracker.txMask, HEX);
      Serial.print(" rx=0x");
      Serial.print(g_bridgeTracker.rxMask, HEX);
      Serial.print(" vpr_pdu=");
      Serial.print(g_bridgeTracker.vprPduQueued);
      Serial.print(" injected=");
      Serial.print(g_bridgeTracker.peerPdusConsumed);
      Serial.print(" direct=");
      Serial.print(g_bridgeTracker.directCommands);
      Serial.print(" local=");
      Serial.print(g_bridgeTracker.localSubeventResults);
      Serial.print(" peer=");
      Serial.print(g_bridgeTracker.peerSubeventResults);
      Serial.print(" proc=");
      Serial.print(g_bridgeTracker.completedProcedureCounter);
      Serial.print(" est=");
      Serial.print(g_bridgeTracker.estimateValid ? 1 : 0);
      Serial.print(" sched=");
      Serial.print(schedulerOk ? 1 : 0);
      Serial.print(" sched_flags=0x");
      Serial.print(scheduler.flags, HEX);
      Serial.print(" sched_stage=");
      Serial.print(scheduler.pendingResultStage);
      Serial.print(" sched_proc=");
      Serial.print(scheduler.procedureCounter);
      Serial.print(" sched_sub=");
      Serial.print(scheduler.activeSubeventIndex);
      Serial.print('/');
      Serial.print(scheduler.totalSubevents);
      Serial.print(" sched_steps=");
      Serial.print(scheduler.totalSteps);
      Serial.print(" sched_chunk=");
      Serial.print(scheduler.localChunkStartStep);
      Serial.print('/');
      Serial.print(scheduler.peerChunkStartStep);
      Serial.print(" sec=");
      Serial.print(securityOk ? 1 : 0);
      Serial.print(" sec_flags=0x");
      Serial.print(security.flags, HEX);
      Serial.print(" sec_conn=0x");
      Serial.print(security.connHandle, HEX);
      Serial.print(" sec_cfg=");
      Serial.print(security.configId);
      Serial.print(" sec_nonce=0x");
      Serial.print(security.drbgNonce, HEX);
      Serial.print(" sec_token=0x");
      Serial.print(security.materialToken, HEX);
      Serial.print(" sec_ctr=");
      Serial.print(security.sessionCounter);
      Serial.print(" work=");
      Serial.print(workOk ? 1 : 0);
      Serial.print(" work_flags=0x");
      Serial.print(work.flags, HEX);
      Serial.print(" work_proc=");
      Serial.print(work.procedureCounter);
      Serial.print(" work_sub=");
      Serial.print(work.activeSubeventIndex);
      Serial.print('/');
      Serial.print(work.totalSubevents);
      Serial.print(" work_steps=");
      Serial.print(work.subeventStepCount);
      Serial.print('/');
      Serial.print(work.totalSteps);
      Serial.print(" work_chunk=");
      Serial.print(work.localChunkStartStep);
      Serial.print('/');
      Serial.print(work.peerChunkStartStep);
      Serial.print(" work_ch=");
      Serial.print(work.stepChannelCount);
      Serial.print(':');
      for (uint8_t i = 0U; i < work.stepChannelCount; ++i) {
        if (i != 0U) {
          Serial.print(',');
        }
        Serial.print(work.stepChannels[i]);
      }
      Serial.print("\r\n");
      printConnectedWindowPlan();
      (void)beginPhysicalFollowup();
    }

    if (!poll.eventStarted) {
      delay(1);
    }
  }

  const uint32_t now = millis();
  if ((now - g_lastStatusMs) >= kStatusIntervalMs) {
    g_lastStatusMs = now;
    printStatus("debug");
  }
}
