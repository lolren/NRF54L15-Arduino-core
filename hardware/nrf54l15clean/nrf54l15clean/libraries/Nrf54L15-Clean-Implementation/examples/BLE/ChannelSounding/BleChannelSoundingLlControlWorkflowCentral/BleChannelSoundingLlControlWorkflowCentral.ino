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
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

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
static constexpr uint8_t kConnectedPhysicalSweepChannels[] = {
    18U, 4U, 32U, 10U, 26U, 1U, 35U, 14U, 22U,
};
static constexpr uint8_t kConnectedPhysicalSweepChannelCount =
    sizeof(kConnectedPhysicalSweepChannels) /
    sizeof(kConnectedPhysicalSweepChannels[0]);
static constexpr uint8_t kConnectedPhysicalMinValidChannels = 3U;
static constexpr uint32_t kConnectedCsSingleChannelWindowUs = 14000UL;
static constexpr uint32_t kConnectedCsFullSweepWindowUs =
    kConnectedCsSingleChannelWindowUs * kPhysicalChannelCount;
static constexpr uint32_t kConnectedCsGuardBeforeUs = 4000UL;
static constexpr uint32_t kConnectedCsGuardAfterUs = 4500UL;
static constexpr uint8_t kConnectedPhysicalTriggerReason = 0x7EU;
static constexpr uint8_t kConnectedPhysicalAckReason = 0x7DU;
static constexpr uint16_t kConnectedPhysicalWindowEventOffset = 4U;

static bool g_wasConnected = false;
static bool g_bridgeStarted = false;
static bool g_passPrinted = false;
static bool g_failed = false;
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

static void printDistanceField(const char* label, float value) {
  Serial.print(label);
  if (isfinite(value)) {
    Serial.print(value, 4);
  } else {
    Serial.print("nan");
  }
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
  g_csConfig.session.workflow.createConfig.minMainModeSteps = 3U;
  g_csConfig.session.workflow.createConfig.maxMainModeSteps = 3U;
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
  radioConfig.enableRtt = false;
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
  Serial.print(kConnectedPhysicalSweepChannelCount);
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
  Serial.print(" work_applied=");
  Serial.print(sweepResult.workItemApplied ? 1 : 0);
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
  config.enableRtt = false;
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
    }
    if (poll.eventServiceCalled) {
      handleBridgeServiceResult(poll.eventService);
    } else if (poll.serviceCalled && !poll.preServiceCalled) {
      handleBridgeServiceResult(poll.service);
    }
    updateWorkflowMask();

    if (!g_passPrinted && g_bridgeTracker.complete()) {
      g_passPrinted = true;
      BleCsVprSchedulerState scheduler{};
      const bool schedulerOk =
          g_csHost.directReadSchedulerStateForTest(&scheduler) &&
          scheduler.valid && scheduler.status == 0U;
      BleCsVprMeasurementWorkItem work{};
      const bool workOk =
          g_csHost.directReadMeasurementWorkItemForTest(&work) &&
          work.valid && work.status == 0U && work.ready;
      (void)runConnectedPhysicalSweep(workOk ? &work : nullptr);
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
