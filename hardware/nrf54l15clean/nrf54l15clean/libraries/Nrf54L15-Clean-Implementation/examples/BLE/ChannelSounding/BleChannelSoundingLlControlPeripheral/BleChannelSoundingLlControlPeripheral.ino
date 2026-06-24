/*
 * BleChannelSoundingLlControlPeripheral
 *
 * Peripheral-side diagnostic for real over-air Channel Sounding LL-control
 * transport. Use with BleChannelSoundingLlControlCentral on a second board.
 *
 * This does not run an RF ranging procedure. It responds to the central with
 * real-shaped CS LL-control PDUs so the central can inject those peer PDUs into
 * the VPR CS peer-exchange state machine.
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

static BleRadio g_ble;
static PowerManager g_power;

static constexpr int8_t kTxPowerDbm = 0;
static constexpr uint32_t kAdvIntervalMs = 80UL;
static constexpr char kAddressText[] = "C0:DE:54:15:C5:11";
static constexpr char kName[] = "XIAO54-CSLL";

static bool g_wasConnected = false;
static uint32_t g_advEvents = 0;
static uint32_t g_linkEvents = 0;
static uint32_t g_rspQueued = 0;
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
        (void)queueCsAbort();
      }
      break;
    default:
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleChannelSoundingLlControlPeripheral start\r\n");

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
}

void loop() {
  if (!g_ble.isConnected()) {
    if (g_wasConnected) {
      g_wasConnected = false;
      Serial.print("disconnected\r\n");
      printDebug("final");
      Gpio::write(kPinUserLed, true);
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
