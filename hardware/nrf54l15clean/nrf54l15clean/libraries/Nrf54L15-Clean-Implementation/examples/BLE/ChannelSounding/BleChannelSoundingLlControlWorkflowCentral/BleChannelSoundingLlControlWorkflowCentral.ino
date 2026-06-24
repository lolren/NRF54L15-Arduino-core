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

static bool g_wasConnected = false;
static bool g_bridgeStarted = false;
static bool g_passPrinted = false;
static bool g_failed = false;
static bool g_physicalStarted = false;
static bool g_physicalReady = false;
static bool g_physicalDone = false;
static bool g_physicalFailed = false;
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

static uint8_t physicalSweepChannelAt(uint8_t order) {
  const uint8_t center = 18U;
  if (order == 0U) {
    return center;
  }

  const uint8_t step = static_cast<uint8_t>((order + 1U) / 2U);
  if ((order & 0x1U) != 0U) {
    return static_cast<uint8_t>(center - step);
  }
  return static_cast<uint8_t>(center + step);
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
  g_bridgeStarted = beginWorkflowBridge();

  Serial.print("workflow bridge init: ");
  Serial.print(g_bridgeStarted ? "OK" : "FAIL");
  Serial.print(" phase=");
  Serial.print(phaseName(g_csHost.workflowState().phase));
  Serial.print("\r\n");
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
  for (uint8_t order = 0U; order < kPhysicalChannelCount; ++order) {
    const uint8_t channel = physicalSweepChannelAt(order);
    BleCsChannelMeasurement measurement{};
    const bool ok = g_physicalCs.measureChannel(channel, g_physicalSequence++,
                                                &measurement);
    g_physicalMeasurements[order] = measurement;
    if (ok && measurement.valid) {
      ++validChannels;
    }
    delayMicroseconds(120U);
  }

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
      Serial.print("\r\n");
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
