/*
 * BleChannelSoundingLlControlPeripheral
 *
 * Peripheral-side diagnostic for real over-air Channel Sounding LL-control
 * transport. Use with BleChannelSoundingLlControlWorkflowCentral on a second
 * board for the Arduino Serial Monitor distance test.
 *
 * This does not run an RF ranging procedure. It responds to the central with
 * real-shaped CS LL-control PDUs so the central can inject those peer PDUs into
 * the VPR CS peer-exchange state machine.
 *
 * Expected terminal output:
 *   cs_serial_reflector=READY ...
 *   connected
 *   connected_physical_reflector ... reply=1 ...
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

static BleRadio g_ble;
static PowerManager g_power;
static BleChannelSoundingRadio g_physicalCs;

static constexpr int8_t kTxPowerDbm = 0;
static constexpr uint32_t kAdvIntervalMs = 80UL;
static constexpr char kAddressText[] = "C0:DE:54:15:C5:11";
static constexpr char kName[] = "XIAO54-CSLL";
static constexpr BoardAntennaPath kPhysicalAntennaPath = BoardAntennaPath::kCeramic;
static constexpr uint32_t kConnectedCsSingleChannelWindowUs = 14000UL;
static constexpr uint32_t kConnectedCsGuardBeforeUs = 1800UL;
static constexpr uint32_t kConnectedCsGuardAfterUs = 4500UL;
static constexpr uint32_t kConnectedCsReflectorListenUs = 16000UL;
static constexpr uint8_t kConnectedPhysicalTriggerReason = 0x7EU;
static constexpr uint8_t kConnectedPhysicalAckReason = 0x7DU;
static constexpr uint16_t kConnectedPhysicalWindowEventOffset = 4U;

static bool g_wasConnected = false;
static bool g_physicalPending = false;
static bool g_physicalMode = false;
static bool g_physicalReady = false;
static bool g_connectedPhysicalPending = false;
static bool g_connectedPhysicalArmed = false;
static bool g_connectedPhysicalAckQueued = false;
static bool g_connectedPhysicalAckSent = false;
static bool g_connectedPhysicalAttempted = false;
static bool g_connectedPhysicalOk = false;
static uint32_t g_connectedPhysicalAckTargetTxSent = 0U;
static uint16_t g_connectedPhysicalAbortTxEventCounter = 0U;
static uint16_t g_connectedPhysicalArmEventCounter = 0U;
static uint16_t g_connectedPhysicalStartEventCounter = 0U;
static uint16_t g_connectedPhysicalRunAfterEventCounter = 0U;
static uint32_t g_advEvents = 0;
static uint32_t g_linkEvents = 0;
static uint32_t g_rspQueued = 0;
static uint32_t g_physicalReplies = 0;
static uint32_t g_lastStatusMs = 0;

static void printOpcode(uint8_t opcode) {
  Serial.print("0x");
  if (opcode < 16U) {
    Serial.print('0');
  }
  Serial.print(opcode, HEX);
}

static void printDebug(const char* prefix) {
  BleChannelSoundingLlControlDebug dbg{};
  g_ble.getChannelSoundingLlControlDebug(&dbg);
  Serial.print(prefix);
  Serial.print(" rx=");
  Serial.print(dbg.rxCount);
  Serial.print(" txq=");
  Serial.print(dbg.txQueuedCount);
  Serial.print(" txsent=");
  Serial.print(dbg.txSentCount);
  Serial.print(" txdrop=");
  Serial.print(dbg.txDropCount);
  Serial.print(" rxdrop=");
  Serial.print(dbg.rxDropCount);
  Serial.print(" last_rx=");
  printOpcode(dbg.lastRxOpcode);
  Serial.print(" last_tx=");
  printOpcode(dbg.lastTxOpcode);
  Serial.print("\r\n");
}

static bool eventCounterReached(uint16_t current, uint16_t target) {
  return static_cast<int16_t>(current - target) >= 0;
}

static bool isConnectedPhysicalTrigger(const BleConnectionEvent& evt) {
  return (evt.llControlOpcode == kBleCsLlCtrlTerminate ||
          evt.llControlOpcode == kBleCsLlCtrlAbort) &&
         evt.payload != nullptr &&
         evt.payloadLength >= 3U &&
         evt.payload[2] == kConnectedPhysicalTriggerReason;
}

static bool queuePdu(const uint8_t* payload, uint8_t length, const char* label) {
  if (!bleCsLlControlPduIsValid(payload, length)) {
    Serial.print("invalid ");
    Serial.print(label);
    Serial.print("\r\n");
    return false;
  }

  if (g_ble.queueChannelSoundingLlControlPdu(payload, length)) {
    ++g_rspQueued;
    Serial.print("queued ");
    Serial.print(label);
    Serial.print("\r\n");
    return true;
  }

  Serial.print("queue failed ");
  Serial.print(label);
  Serial.print("\r\n");
  return false;
}

static bool queueCsResponse(uint8_t configId) {
  BleCsLlControlPdu pdu{};
  if (!bleCsBuildLlControlRsp(configId, true, &pdu)) {
    return false;
  }
  return queuePdu(pdu.data(), pdu.length, "CS_RSP");
}

static bool queueCsConfig(uint8_t configId) {
  BleCsLlControlConfigParams params{};
  params.configId = configId;
  BleCsLlControlPdu pdu{};
  if (!bleCsBuildLlControlConfig(params, &pdu)) {
    return false;
  }
  return queuePdu(pdu.data(), pdu.length, "CS_CFG");
}

static bool queueCsSecurityResponse(uint8_t configId) {
  BleCsLlControlPdu pdu{};
  if (!bleCsBuildLlControlSecurityRsp(configId, &pdu)) {
    return false;
  }
  return queuePdu(pdu.data(), pdu.length, "CS_SEC_RSP");
}

static bool queueCsProcedureResponse(uint8_t configId) {
  BleCsLlControlProcedureParams params{};
  params.configId = configId;
  BleCsLlControlPdu pdu{};
  if (!bleCsBuildLlControlProcedureRsp(params, &pdu)) {
    return false;
  }
  return queuePdu(pdu.data(), pdu.length, "CS_PROC_RSP");
}

static bool queueCsStart(uint8_t configId) {
  BleCsLlControlStartParams params{};
  params.configId = configId;
  BleCsLlControlPdu pdu{};
  if (!bleCsBuildLlControlStart(params, &pdu)) {
    return false;
  }
  return queuePdu(pdu.data(), pdu.length, "CS_START");
}

static bool queueCsAbort() {
  BleCsLlControlPdu pdu{};
  if (!bleCsBuildLlControlAbort(0x42U, &pdu)) {
    return false;
  }
  return queuePdu(pdu.data(), pdu.length, "CS_ABORT");
}

static bool queueConnectedPhysicalAck() {
  BleCsLlControlPdu pdu{};
  if (!bleCsBuildLlControlAbort(kConnectedPhysicalAckReason, &pdu)) {
    return false;
  }
  BleChannelSoundingLlControlDebug dbg{};
  g_ble.getChannelSoundingLlControlDebug(&dbg);
  const bool queued = queuePdu(pdu.data(), pdu.length, "CS_CONNECTED_ACK");
  if (queued) {
    g_connectedPhysicalAckTargetTxSent = dbg.txSentCount + 1U;
  }
  return queued;
}

static uint8_t configIdFromEvent(const BleConnectionEvent& evt) {
  if (evt.payload == nullptr || evt.payloadLength < 3U) {
    return 0U;
  }
  if (evt.payload[1] == 0U) {
    return 0U;
  }
  return evt.payload[2];
}

static void respondToCsControl(const BleConnectionEvent& evt) {
  const uint8_t configId = configIdFromEvent(evt);
  switch (evt.llControlOpcode) {
    case kBleCsLlCtrlReq:
      if (evt.payloadLength >= 4U && evt.payload[1] == 0x02U) {
        (void)queueCsResponse(configId);
        (void)queueCsConfig(configId);
      }
      break;
    case kBleCsLlCtrlSecReq:
      if (evt.payloadLength >= 3U && evt.payload[1] == 0x01U) {
        (void)queueCsSecurityResponse(configId);
      }
      break;
    case kBleCsLlCtrlProcReq:
      if (evt.payloadLength >= 21U && evt.payload[1] == 19U) {
        (void)queueCsProcedureResponse(configId);
        (void)queueCsStart(configId);
        if (queueCsAbort()) {
          g_physicalPending = true;
        }
      }
      break;
    case kBleCsLlCtrlTerminate:
    case kBleCsLlCtrlAbort:
      if (g_physicalPending && isConnectedPhysicalTrigger(evt)) {
        g_connectedPhysicalPending = true;
        g_connectedPhysicalAckQueued = queueConnectedPhysicalAck();
        g_connectedPhysicalArmed = g_connectedPhysicalAckQueued;
        g_connectedPhysicalAckSent = false;
        g_connectedPhysicalAttempted = false;
        g_connectedPhysicalOk = false;
        g_connectedPhysicalAbortTxEventCounter = evt.eventCounter;
        g_connectedPhysicalStartEventCounter =
            static_cast<uint16_t>(evt.eventCounter +
                                  kConnectedPhysicalWindowEventOffset);
        g_connectedPhysicalArmEventCounter =
            static_cast<uint16_t>(g_connectedPhysicalStartEventCounter - 1U);
        g_connectedPhysicalRunAfterEventCounter = 0U;
      }
      break;
    default:
      break;
  }
}

static bool runConnectedPhysicalReflectorOnce() {
  if (!g_connectedPhysicalPending || g_connectedPhysicalAttempted) {
    return g_connectedPhysicalOk;
  }

  g_connectedPhysicalAttempted = true;
  g_connectedPhysicalOk = false;

  BleCsConfig config;
  config.txPowerDbm = -8;
  config.controlChannel = 37U;
  config.probeToReportDelayUs = 1000U;
  config.controlListenWindowUs = kConnectedCsReflectorListenUs;
  config.probeListenWindowUs = 10000U;
  config.maxPayloadLength = 32U;
  config.minToneMagnitude = 8U;
  config.enableRtt = false;
  config.enableRawDfeCapture = true;

  const bool rfOk = BoardControl::enableRfPath(kPhysicalAntennaPath);
  const bool rawOk = rfOk && g_physicalCs.begin(config);
  BleConnectionTimingSnapshot snapshot{};
  BleCsConnectedWindowPlan plan{};
  const bool snapshotOk = rawOk && g_ble.getConnectionTimingSnapshot(&snapshot);
  const bool fits =
      snapshotOk && BleChannelSoundingRadio::planConnectedWindow(
                        snapshot, kConnectedCsSingleChannelWindowUs,
                        kConnectedCsGuardBeforeUs, kConnectedCsGuardAfterUs,
                        &plan);

  uint32_t elapsedUs = 0U;
  if (fits) {
    const uint32_t waitStartUs = micros();
    while (static_cast<uint32_t>(micros() - waitStartUs) <
           kConnectedCsGuardBeforeUs) {
      __asm volatile("nop");
    }
    const uint32_t listenStartUs = micros();
    g_connectedPhysicalOk =
        g_physicalCs.listenAndReflectOnce(kConnectedCsReflectorListenUs);
    elapsedUs = static_cast<uint32_t>(micros() - listenStartUs);
  }
  const BleCsReflectorTiming timing = g_physicalCs.lastReflectorTiming();
  const BleCsDfeCaptureInfo dfeInfo = g_physicalCs.lastDfeCaptureInfo();
  g_physicalCs.end();

  Serial.print("connected_physical_reflector snapshot=");
  Serial.print(snapshotOk ? 1 : 0);
  Serial.print(" abort_ce=");
  Serial.print(g_connectedPhysicalAbortTxEventCounter);
  Serial.print(" run_after_ce=");
  Serial.print(g_connectedPhysicalRunAfterEventCounter);
  Serial.print(" raw_before_ce=");
  Serial.print(g_connectedPhysicalStartEventCounter);
  Serial.print(" rf=");
  Serial.print(rfOk ? 1 : 0);
  Serial.print(" raw=");
  Serial.print(rawOk ? 1 : 0);
  Serial.print(" fit=");
  Serial.print(fits ? 1 : 0);
  Serial.print(" reply=");
  Serial.print(g_connectedPhysicalOk ? 1 : 0);
  Serial.print(" status=");
  Serial.print(g_physicalCs.lastReflectorStatus());
  Serial.print(" avail_us=");
  Serial.print(plan.availableUs);
  Serial.print(" elapsed_us=");
  Serial.print(elapsedUs);
  Serial.print(" ctrl_rx_us=");
  Serial.print(timing.controlRxUs);
  Serial.print(" probe_gap_us=");
  Serial.print(timing.controlToProbeRxGapUs);
  Serial.print(" probe_rx_us=");
  Serial.print(timing.probeRxUs);
  Serial.print(" report_gap_us=");
  Serial.print(timing.probeToReportGapUs);
  Serial.print(" report_tx_us=");
  Serial.print(timing.reportTxUs);
  Serial.print(" dfe_present=");
  Serial.print(dfeInfo.present ? 1 : 0);
  Serial.print(" dfe_zero=");
  Serial.print(dfeInfo.allZero ? 1 : 0);
  Serial.print(" dfe_amount=");
  Serial.print(dfeInfo.amountBytes);
  Serial.print(" dfe_current=");
  Serial.print(dfeInfo.currentAmountBytes);
  Serial.print(" reason=");
  Serial.print(snapshotOk ? plan.reason : 11U);
  Serial.print("\r\n");

  return g_connectedPhysicalOk;
}

static bool beginPhysicalReflector() {
  g_physicalMode = true;
  g_physicalReady = false;
  g_physicalReplies = 0U;

  g_ble.end();
  delay(250);

  BleCsConfig config;
  config.txPowerDbm = -8;
  config.controlChannel = 37U;
  config.probeToReportDelayUs = 1200U;
  config.controlListenWindowUs = 20000U;
  config.probeListenWindowUs = 8000U;
  config.maxPayloadLength = 32U;
  config.minToneMagnitude = 16U;
  config.enableRtt = false;
  config.enableRawDfeCapture = true;

  const bool rfOk = BoardControl::enableRfPath(kPhysicalAntennaPath);
  g_physicalReady = rfOk && g_physicalCs.begin(config);
  Serial.print("physical reflector init: rf=");
  Serial.print(rfOk ? 1 : 0);
  Serial.print(" raw=");
  Serial.print(g_physicalReady ? 1 : 0);
  Serial.print("\r\n");
  return g_physicalReady;
}

static void runPhysicalReflector() {
  if (!g_physicalReady) {
    delay(100);
    return;
  }

  if (g_physicalCs.listenAndReflectOnce()) {
    ++g_physicalReplies;
  }

  const uint32_t now = millis();
  if ((now - g_lastStatusMs) >= 1000UL) {
    g_lastStatusMs = now;
    Serial.print("physical reflector replies=");
    Serial.print(g_physicalReplies);
    Serial.print("\r\n");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleChannelSoundingLlControlPeripheral start\r\n");
  Serial.print("Arduino CS serial test reflector: leave this board powered ");
  Serial.print("and watch the central board for cs_distance_m lines.\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  if (ok) {
    ok = g_ble.setDeviceAddressString(kAddressText, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingChannelSelectionAlgorithm2(false) &&
         g_ble.setAdvertisingName(kName, true) &&
         g_ble.setScanResponseName("XIAO54-CSLL-SCAN") &&
         g_ble.setGattDeviceName(kName);
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\naddr=");
  Serial.print(kAddressText);
  Serial.print("\r\n");
  Serial.print("cs_serial_reflector=READY baud=115200 addr=");
  Serial.print(kAddressText);
  Serial.print(" name=");
  Serial.print(kName);
  Serial.print("\r\n");
}

void loop() {
  if (g_physicalMode) {
    runPhysicalReflector();
    return;
  }

  if (!g_ble.isConnected()) {
    if (g_wasConnected) {
      g_wasConnected = false;
      Serial.print("disconnected\r\n");
      printDebug("final");
      Gpio::write(kPinUserLed, true);
      if (g_physicalPending) {
        Serial.print("physical reflector pending\r\n");
        (void)beginPhysicalReflector();
        return;
      }
    }

    BleAdvInteraction adv{};
    (void)g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    ++g_advEvents;
    if (adv.receivedConnectInd) {
      Serial.print("CONNECT_IND received\r\n");
    }

    const uint32_t now = millis();
    if ((now - g_lastStatusMs) >= 1000UL) {
      g_lastStatusMs = now;
      Serial.print("advertising ev=");
      Serial.print(g_advEvents);
      Serial.print("\r\n");
    }
    delay(kAdvIntervalMs);
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_ble.clearChannelSoundingLlControlDebug();
    g_linkEvents = 0U;
    g_rspQueued = 0U;
    g_connectedPhysicalPending = false;
    g_connectedPhysicalArmed = false;
    g_connectedPhysicalAckQueued = false;
    g_connectedPhysicalAckSent = false;
    g_connectedPhysicalAckTargetTxSent = 0U;
    g_connectedPhysicalAttempted = false;
    g_connectedPhysicalOk = false;
    g_connectedPhysicalAbortTxEventCounter = 0U;
    g_connectedPhysicalArmEventCounter = 0U;
    g_connectedPhysicalStartEventCounter = 0U;
    g_connectedPhysicalRunAfterEventCounter = 0U;
    Serial.print("connected\r\n");
    Gpio::write(kPinUserLed, false);
  }

  BleConnectionEvent evt{};
  const bool ran = g_ble.pollConnectionEvent(&evt, 450000UL);
  if (ran && evt.eventStarted) {
    ++g_linkEvents;
    if (evt.packetReceived && evt.crcOk && evt.packetIsNew &&
        evt.channelSoundingLlControlPacket) {
      Serial.print("CS LL RX ce=");
      Serial.print(evt.eventCounter);
      Serial.print(" op=");
      printOpcode(evt.llControlOpcode);
      Serial.print(" len=");
      Serial.print(evt.payloadLength);
      Serial.print("\r\n");
      respondToCsControl(evt);
    }
    if (g_connectedPhysicalPending && g_connectedPhysicalAckQueued &&
        !g_connectedPhysicalAckSent) {
      BleChannelSoundingLlControlDebug dbg{};
      g_ble.getChannelSoundingLlControlDebug(&dbg);
      if (g_connectedPhysicalAckTargetTxSent > 0U &&
          dbg.txSentCount >= g_connectedPhysicalAckTargetTxSent) {
        g_connectedPhysicalAckSent = true;
      }
    }
    if (g_connectedPhysicalPending && g_connectedPhysicalArmed &&
        !g_connectedPhysicalAttempted) {
      if (eventCounterReached(evt.eventCounter,
                              g_connectedPhysicalArmEventCounter)) {
        g_connectedPhysicalRunAfterEventCounter = evt.eventCounter;
        (void)runConnectedPhysicalReflectorOnce();
      }
    }

    const uint32_t now = millis();
    if ((now - g_lastStatusMs) >= 1000UL) {
      g_lastStatusMs = now;
      Serial.print("link ev=");
      Serial.print(g_linkEvents);
      Serial.print(" queued=");
      Serial.print(g_rspQueued);
      Serial.print("\r\n");
      printDebug("debug");
    }
  } else {
    delay(1);
  }
}
