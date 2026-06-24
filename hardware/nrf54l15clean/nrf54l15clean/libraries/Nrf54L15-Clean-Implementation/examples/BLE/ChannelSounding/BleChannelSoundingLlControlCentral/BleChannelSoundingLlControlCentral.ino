/*
 * BleChannelSoundingLlControlCentral
 *
 * Central-side diagnostic for real over-air Channel Sounding LL-control
 * transport. Use with BleChannelSoundingLlControlPeripheral on a second board.
 *
 * This connects to the fixed peripheral address, sends real-shaped CS
 * LL-control PDUs over LLID=control, then injects the received peer PDUs into
 * the existing VPR CS peer-exchange state machine. No GATT/L2CAP wrapping is
 * used. This proves the raw BLE link-layer transport and the VPR peer state
 * machine are connected; it does not perform RF channel sounding yet.
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
static constexpr uint8_t kConfigId = 1U;
static constexpr uint8_t kPeripheralAddress[6] = {
    0x11U, 0xC5U, 0x15U, 0x54U, 0xDEU, 0xC0U,
};
static constexpr uint32_t kStatusIntervalMs = 1000UL;

enum class BridgePhase : uint8_t {
  kConnect = 0U,
  kSendCsReq,
  kWaitConfig,
  kSendSecurityReq,
  kWaitSecurity,
  kSendProcReq,
  kWaitProcedure,
  kComplete,
  kFailed,
};

static bool g_wasConnected = false;
static bool g_vprReady = false;
static BridgePhase g_phase = BridgePhase::kConnect;
static uint32_t g_connectAttempts = 0U;
static uint32_t g_linkEvents = 0U;
static uint32_t g_txQueued = 0U;
static uint32_t g_peerPdusInjected = 0U;
static uint32_t g_lastStatusMs = 0U;
static uint8_t g_lastVprStage = 0xFFU;
static uint8_t g_lastVprStatus = 0xFFU;
static uint32_t g_progressMask = 0U;

static void markProgress(uint8_t bit) {
  if (bit < 32U) {
    g_progressMask |= (1UL << bit);
  }
}

static void printOpcode(uint8_t opcode) {
  Serial.print("0x");
  if (opcode < 16U) {
    Serial.print('0');
  }
  Serial.print(opcode, HEX);
}

static void printPhase() {
  switch (g_phase) {
    case BridgePhase::kConnect: Serial.print("connect"); break;
    case BridgePhase::kSendCsReq: Serial.print("send_cs_req"); break;
    case BridgePhase::kWaitConfig: Serial.print("wait_config"); break;
    case BridgePhase::kSendSecurityReq: Serial.print("send_sec_req"); break;
    case BridgePhase::kWaitSecurity: Serial.print("wait_security"); break;
    case BridgePhase::kSendProcReq: Serial.print("send_proc_req"); break;
    case BridgePhase::kWaitProcedure: Serial.print("wait_procedure"); break;
    case BridgePhase::kComplete: Serial.print("complete"); break;
    case BridgePhase::kFailed: Serial.print("failed"); break;
  }
}

static void printDebug(const char* prefix) {
  BleChannelSoundingLlControlDebug dbg{};
  g_ble.getChannelSoundingLlControlDebug(&dbg);
  Serial.print(prefix);
  Serial.print(" phase=");
  printPhase();
  Serial.print(" ev=");
  Serial.print(g_linkEvents);
  Serial.print(" txq=");
  Serial.print(g_txQueued);
  Serial.print(" inj=");
  Serial.print(g_peerPdusInjected);
  Serial.print(" ble_rx=");
  Serial.print(dbg.rxCount);
  Serial.print(" ble_txsent=");
  Serial.print(dbg.txSentCount);
  Serial.print(" ble_txdrop=");
  Serial.print(dbg.txDropCount);
  Serial.print(" ble_rxdrop=");
  Serial.print(dbg.rxDropCount);
  Serial.print(" vpr_stage=");
  Serial.print(g_lastVprStage);
  Serial.print(" vpr_status=0x");
  Serial.print(g_lastVprStatus, HEX);
  Serial.print(" progress=0x");
  Serial.print(g_progressMask, HEX);
  Serial.print(" last_rx=");
  printOpcode(dbg.lastRxOpcode);
  Serial.print(" last_tx=");
  printOpcode(dbg.lastTxOpcode);
  Serial.print("\r\n");
}

static bool queuePdu(const uint8_t* payload, uint8_t length, const char* label) {
  if (g_ble.queueChannelSoundingLlControlPdu(payload, length)) {
    ++g_txQueued;
    Serial.print("queued ");
    Serial.print(label);
    Serial.print("\r\n");
    return true;
  }

  Serial.print("queue failed ");
  Serial.print(label);
  Serial.print("\r\n");
  g_phase = BridgePhase::kFailed;
  return false;
}

static bool queueCsReq() {
  const uint8_t payload[] = {
      kBleCsLlCtrlReq,
      0x02U,
      0x01U,
      kConfigId,
  };
  return queuePdu(payload, sizeof(payload), "CS_REQ");
}

static bool queueCsSecReq() {
  const uint8_t payload[] = {
      kBleCsLlCtrlSecReq,
      0x01U,
      kConfigId,
  };
  return queuePdu(payload, sizeof(payload), "CS_SEC_REQ");
}

static bool queueCsProcReq() {
  uint8_t payload[21] = {
      kBleCsLlCtrlProcReq,
      19U,
      kConfigId,
  };
  payload[3] = 0x80U;  // max procedure length, little-endian low byte
  payload[4] = 0x0CU;
  payload[5] = 0x18U;  // min interval = 30 ms
  payload[6] = 0x00U;
  payload[7] = 0x18U;  // max interval = 30 ms
  payload[8] = 0x00U;
  payload[9] = 0x01U;  // max procedure count
  payload[10] = 0x00U;
  payload[11] = 0x80U; // min subevent length
  payload[12] = 0x0CU;
  payload[15] = 0x80U; // max subevent length
  payload[16] = 0x0CU;
  payload[19] = 0x01U; // LE 1M PHY
  payload[20] = 0x00U; // TX power delta
  return queuePdu(payload, sizeof(payload), "CS_PROC_REQ");
}

static bool bootVprPeerBridge() {
  BleCsControllerVprHost::fillDemoConfig(&g_csConfig);
  g_csConfig.builtInPeerDemo.enabled = false;
  g_csConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  g_csConfig.session.workflow.procedureEnable.enable = 1U;

  bool ok = g_csHost.resetTransport(true) &&
            g_csHost.loadDefaultTransportImage() &&
            g_csHost.bootTransport() &&
            g_csHost.beginHost(kCsConnHandle, g_csConfig) &&
            !g_csHost.failed();
  if (ok) markProgress(0U);

  uint8_t status = 0xFFU;
  ok = ok && g_csHost.directReadRemoteSupportedCapabilities(&status) &&
       status == 0U;
  g_lastVprStatus = status;
  if (ok) markProgress(1U);

  ok = ok && g_csHost.directSetDefaultSettings(
                 g_csConfig.session.workflow.defaultSettings, &status) &&
       status == 0U;
  g_lastVprStatus = status;
  if (ok) markProgress(2U);

  ok = ok && g_csHost.directCreateConfig(
                 g_csConfig.session.workflow.createConfig, &status) &&
       status == 0U;
  g_lastVprStatus = status;
  if (ok) markProgress(3U);

  BleCsVprPeerExchangeState state{};
  ok = ok && g_csHost.directReadPeerExchangeStateForTest(&state) &&
       state.valid &&
       state.currentStage == kBleCsVprPeerStageAwaitingCsRsp;
  g_lastVprStage = state.currentStage;
  g_lastVprStatus = state.status;
  if (ok) markProgress(4U);
  return ok;
}

static bool injectPeerPdu(const uint8_t* payload, uint8_t length) {
  BleCsVprPeerExchangeState state{};
  if (!g_csHost.directInjectPeerPduForTest(payload, length, &state) ||
      !state.valid || state.status != 0U) {
    g_lastVprStatus = state.status;
    g_lastVprStage = state.currentStage;
    g_phase = BridgePhase::kFailed;
    Serial.print("VPR inject failed status=0x");
    Serial.print(state.status, HEX);
    Serial.print(" stage=");
    Serial.print(state.currentStage);
    Serial.print("\r\n");
    return false;
  }

  ++g_peerPdusInjected;
  g_lastVprStage = state.currentStage;
  g_lastVprStatus = state.status;
  Serial.print("VPR inject op=");
  printOpcode(payload[0]);
  Serial.print(" prev=");
  Serial.print(state.previousStage);
  Serial.print(" stage=");
  Serial.print(state.currentStage);
  Serial.print("\r\n");
  return true;
}

static void advanceAfterPeerPdu(uint8_t opcode) {
  if (g_phase == BridgePhase::kWaitConfig &&
      opcode == kBleCsLlCtrlRsp &&
      g_lastVprStage == kBleCsVprPeerStageAwaitingCsCfg) {
    markProgress(5U);
    return;
  }

  if (g_phase == BridgePhase::kWaitConfig &&
      opcode == kBleCsLlCtrlCfg &&
      g_lastVprStage == kBleCsVprPeerStageIdle) {
    markProgress(6U);
    g_phase = BridgePhase::kSendSecurityReq;
    return;
  }

  if (g_phase == BridgePhase::kWaitSecurity &&
      opcode == kBleCsLlCtrlSecRsp &&
      g_lastVprStage == kBleCsVprPeerStageIdle) {
    markProgress(8U);
    g_phase = BridgePhase::kSendProcReq;
    return;
  }

  if (g_phase == BridgePhase::kWaitProcedure &&
      opcode == kBleCsLlCtrlProcRsp &&
      g_lastVprStage == kBleCsVprPeerStageAwaitingStart) {
    uint8_t status = 0xFFU;
    if (g_csHost.directProcedureEnable(
            g_csConfig.session.workflow.procedureEnable, &status) &&
        status == 0U) {
      g_lastVprStatus = status;
      markProgress(10U);
    } else {
      g_lastVprStatus = status;
      g_phase = BridgePhase::kFailed;
    }
    return;
  }

  if (g_phase == BridgePhase::kWaitProcedure &&
      opcode == kBleCsLlCtrlStart &&
      g_lastVprStage == kBleCsVprPeerStageProcedureActive) {
    markProgress(11U);
    return;
  }

  if (g_phase == BridgePhase::kWaitProcedure &&
      opcode == kBleCsLlCtrlAbort &&
      g_lastVprStage == kBleCsVprPeerStageIdle) {
    markProgress(12U);
    g_phase = BridgePhase::kComplete;
    Serial.print("cs_ll_vpr_bridge=PASS progress=0x");
    Serial.print(g_progressMask, HEX);
    Serial.print(" injected=");
    Serial.print(g_peerPdusInjected);
    Serial.print("\r\n");
  }
}

static void resetBridgeState() {
  g_ble.clearChannelSoundingLlControlDebug();
  g_linkEvents = 0U;
  g_txQueued = 0U;
  g_peerPdusInjected = 0U;
  g_lastVprStage = 0xFFU;
  g_lastVprStatus = 0xFFU;
  g_progressMask = 0U;
  g_vprReady = bootVprPeerBridge();
  g_phase = g_vprReady ? BridgePhase::kSendCsReq : BridgePhase::kFailed;
  Serial.print("VPR bridge init: ");
  Serial.print(g_vprReady ? "OK" : "FAIL");
  Serial.print(" stage=");
  Serial.print(g_lastVprStage);
  Serial.print(" status=0x");
  Serial.print(g_lastVprStatus, HEX);
  Serial.print("\r\n");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleChannelSoundingLlControlCentral start\r\n");

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
      printDebug("final");
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

  if (g_phase == BridgePhase::kSendCsReq) {
    if (queueCsReq()) {
      markProgress(4U);
      g_phase = BridgePhase::kWaitConfig;
    }
  } else if (g_phase == BridgePhase::kSendSecurityReq) {
    uint8_t status = 0xFFU;
    if (g_csHost.directSecurityEnable(&status) && status == 0U &&
        queueCsSecReq()) {
      g_lastVprStatus = status;
      markProgress(7U);
      g_phase = BridgePhase::kWaitSecurity;
    } else {
      g_lastVprStatus = status;
      g_phase = BridgePhase::kFailed;
    }
  } else if (g_phase == BridgePhase::kSendProcReq) {
    uint8_t status = 0xFFU;
    if (g_csHost.directSetProcedureParameters(
            g_csConfig.session.workflow.procedureParameters, &status) &&
        status == 0U && queueCsProcReq()) {
      g_lastVprStatus = status;
      markProgress(9U);
      g_phase = BridgePhase::kWaitProcedure;
    } else {
      g_lastVprStatus = status;
      g_phase = BridgePhase::kFailed;
    }
  }

  BleConnectionEvent evt{};
  const bool ran = g_ble.pollConnectionEvent(&evt, 450000UL);
  if (ran && evt.eventStarted) {
    ++g_linkEvents;
    if (evt.packetReceived && evt.crcOk && evt.packetIsNew &&
        evt.channelSoundingLlControlPacket && evt.payload != nullptr &&
        evt.payloadLength >= 2U) {
      Serial.print("CS LL RX ce=");
      Serial.print(evt.eventCounter);
      Serial.print(" op=");
      printOpcode(evt.llControlOpcode);
      Serial.print(" len=");
      Serial.print(evt.payloadLength);
      Serial.print("\r\n");
      if (injectPeerPdu(evt.payload, evt.payloadLength)) {
        advanceAfterPeerPdu(evt.llControlOpcode);
      }
    }
  } else {
    delay(1);
  }

  const uint32_t now = millis();
  if ((now - g_lastStatusMs) >= kStatusIntervalMs) {
    g_lastStatusMs = now;
    printDebug("debug");
  }
}
