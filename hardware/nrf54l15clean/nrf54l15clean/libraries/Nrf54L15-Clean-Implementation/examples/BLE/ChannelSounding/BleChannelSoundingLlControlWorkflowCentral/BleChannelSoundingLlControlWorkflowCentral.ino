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

static constexpr int8_t kTxPowerDbm = 0;
static constexpr uint16_t kCsConnHandle = 0x0041U;
static constexpr uint8_t kPeripheralAddress[6] = {
    0x11U, 0xC5U, 0x15U, 0x54U, 0xDEU, 0xC0U,
};
static constexpr uint32_t kStatusIntervalMs = 1000UL;

static bool g_wasConnected = false;
static bool g_bridgeStarted = false;
static bool g_passPrinted = false;
static bool g_failed = false;
static uint32_t g_connectAttempts = 0U;
static uint32_t g_linkEvents = 0U;
static uint32_t g_txQueued = 0U;
static uint32_t g_peerPdusInjected = 0U;
static uint32_t g_directCommands = 0U;
static uint32_t g_lastStatusMs = 0U;
static uint32_t g_workflowMask = 0U;
static uint32_t g_txMask = 0U;
static uint32_t g_rxMask = 0U;
static uint8_t g_lastVprStage = 0xFFU;
static uint8_t g_lastVprStatus = 0xFFU;

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
    markTxOpcode(result.txOpcode);
    Serial.print("queued op=");
    printOpcode(result.txOpcode);
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
  g_peerPdusInjected = 0U;
  g_directCommands = 0U;
  g_workflowMask = 0U;
  g_txMask = 0U;
  g_rxMask = 0U;
  g_lastVprStage = 0xFFU;
  g_lastVprStatus = 0xFFU;
  g_passPrinted = false;
  g_failed = false;
  g_bridgeStarted = beginWorkflowBridge();

  Serial.print("workflow bridge init: ");
  Serial.print(g_bridgeStarted ? "OK" : "FAIL");
  Serial.print(" phase=");
  Serial.print(phaseName(g_csHost.workflowState().phase));
  Serial.print("\r\n");
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
  if (!g_csHost.loopOnceWithInitiatorLlControlBridge(g_ble, &poll, 450000UL)) {
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

    if (!g_passPrinted && bridgeComplete() && resultPathComplete()) {
      g_passPrinted = true;
      Serial.print("cs_ll_workflow_bridge=PASS wf=0x");
      Serial.print(g_workflowMask, HEX);
      Serial.print(" tx=0x");
      Serial.print(g_txMask, HEX);
      Serial.print(" rx=0x");
      Serial.print(g_rxMask, HEX);
      Serial.print(" injected=");
      Serial.print(g_peerPdusInjected);
      Serial.print(" direct=");
      Serial.print(g_directCommands);
      Serial.print(" local=");
      Serial.print(g_csHost.hostState().localSubeventResults);
      Serial.print(" peer=");
      Serial.print(g_csHost.hostState().peerSubeventResults);
      Serial.print(" proc=");
      Serial.print(g_csHost.sessionState().completedProcedureCounter);
      Serial.print(" est=");
      Serial.print(g_csHost.estimateValid() ? 1 : 0);
      Serial.print("\r\n");
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
