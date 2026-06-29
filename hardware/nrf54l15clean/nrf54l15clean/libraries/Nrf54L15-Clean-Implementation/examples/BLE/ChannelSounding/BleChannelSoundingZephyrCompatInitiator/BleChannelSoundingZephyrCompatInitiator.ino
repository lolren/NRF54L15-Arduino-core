/*
  BleChannelSoundingZephyrCompatInitiator

  Zephyr-compatible Channel Sounding step-data GATT bridge.

  Use this with the Zephyr Bluetooth channel_sounding reflector sample, or with
  BleChannelSoundingZephyrCompatReflector on a second Arduino board. It exposes
  the same CS Sample service/characteristic UUIDs used by Zephyr and verifies
  512-byte ATT Prepare Write / Execute Write transfer in both directions.

  This example is the host/service interoperability bridge for CS. The lower
  level LL-control and RF measurement diagnostics remain in the existing
  BleChannelSoundingLlControlWorkflowCentral/Peripheral examples.
*/

#include <Arduino.h>
#include <bluefruit.h>
#include <nrf54l15_hal.h>

using xiao_nrf54l15::BoardAntennaPath;
using xiao_nrf54l15::BoardControl;
using xiao_nrf54l15::PowerLatencyMode;
using xiao_nrf54l15::PowerManager;

namespace {

static constexpr char kZephyrCsName[] = "CS Sample";
static constexpr uint16_t kStepDataLen = 512U;
static constexpr uint32_t kWriteIntervalMs = 3000UL;
static constexpr uint32_t kReconnectDelayMs = 500UL;
static constexpr int8_t kTxPowerDbm = 0;

static PowerManager g_power;
static uint8_t g_localStepData[kStepDataLen];
static uint8_t g_peerStepData[kStepDataLen];
static volatile bool g_connected = false;
static volatile bool g_peerStepWriteSeen = false;
static volatile bool g_scanRestartPending = false;
static uint16_t g_connHandle = BLE_CONN_HANDLE_INVALID;
static uint32_t g_lastWriteMs = 0;
static uint32_t g_localSequence = 0;
static uint32_t g_peerWriteCount = 0;
static uint32_t g_peerWriteBytes = 0;
static uint32_t g_peerWriteCrc = 0;
static bool g_peerStepCharReady = false;

BLEService localStepService("87654321-4567-2389-1254-f67f9fedcba9");
BLECharacteristic localStepChar("87654321-4567-2389-1254-f67f9fedcba8");
BLEClientService peerStepService("87654321-4567-2389-1254-f67f9fedcba9");
BLEClientCharacteristic peerStepChar("87654321-4567-2389-1254-f67f9fedcba8");

static uint32_t fnv1a(const uint8_t* data, uint16_t len) {
  uint32_t hash = 2166136261UL;
  for (uint16_t i = 0; i < len; ++i) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

static void fillStepData(uint32_t sequence) {
  for (uint16_t i = 0; i < kStepDataLen; ++i) {
    g_localStepData[i] =
        static_cast<uint8_t>((i * 37U) ^ (sequence * 13U) ^ 0x5AU);
  }

  // Small recognizable header for serial logs and external decoders.
  g_localStepData[0] = 'C';
  g_localStepData[1] = 'S';
  g_localStepData[2] = 'I';
  g_localStepData[3] = 1U;
  g_localStepData[4] = static_cast<uint8_t>(sequence);
  g_localStepData[5] = static_cast<uint8_t>(sequence >> 8U);
  g_localStepData[6] = static_cast<uint8_t>(sequence >> 16U);
  g_localStepData[7] = static_cast<uint8_t>(sequence >> 24U);
}

static bool reportHasName(const ble_gap_evt_adv_report_t* report,
                          const char* expected) {
  if (report == nullptr || expected == nullptr) {
    return false;
  }

  char name[32] = {0};
  int len = Bluefruit.Scanner.parseReportByType(
      report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, name, sizeof(name) - 1U);
  if (len <= 0) {
    len = Bluefruit.Scanner.parseReportByType(
        report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, name, sizeof(name) - 1U);
  }
  if (len <= 0) {
    return false;
  }
  name[min(static_cast<size_t>(len), sizeof(name) - 1U)] = '\0';
  return strcmp(name, expected) == 0;
}

static void printHex32(uint32_t value) {
  Serial.print("0x");
  for (int shift = 28; shift >= 0; shift -= 4) {
    const uint8_t nibble = static_cast<uint8_t>((value >> shift) & 0x0FU);
    Serial.print(static_cast<char>((nibble < 10U) ? ('0' + nibble)
                                                  : ('A' + nibble - 10U)));
  }
}

static void printBridgeStatus(const char* label, uint16_t bytes,
                              uint32_t crc) {
  Serial.print(label);
  Serial.print(" bytes=");
  Serial.print(bytes);
  Serial.print(" crc=");
  printHex32(crc);
  Serial.print(" peer_writes=");
  Serial.print(g_peerWriteCount);
  Serial.println();
}

static void configureBoardPower() {
  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  BoardControl::setImuMicEnabled(false);
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::enableRfPath(BoardAntennaPath::kCeramic);
}

static void localStepWriteCallback(uint16_t connHandle,
                                   BLECharacteristic* characteristic,
                                   uint8_t* data, uint16_t len) {
  (void)connHandle;
  (void)characteristic;
  const uint16_t copyLen = min<uint16_t>(len, sizeof(g_peerStepData));
  memcpy(g_peerStepData, data, copyLen);
  if (copyLen < sizeof(g_peerStepData)) {
    memset(&g_peerStepData[copyLen], 0, sizeof(g_peerStepData) - copyLen);
  }
  g_peerWriteBytes = copyLen;
  g_peerWriteCrc = fnv1a(g_peerStepData, copyLen);
  ++g_peerWriteCount;
  g_peerStepWriteSeen = true;
}

static void scanCallback(ble_gap_evt_adv_report_t* report) {
  if (report == nullptr) {
    return;
  }

  if (reportHasName(report, kZephyrCsName) ||
      Bluefruit.Scanner.checkReportForService(report, peerStepService)) {
    Bluefruit.Central.connect(report);
    return;
  }
  Bluefruit.Scanner.resume();
}

static void connectCallback(uint16_t connHandle) {
  g_connHandle = connHandle;
  g_connected = false;
  g_peerStepCharReady = false;
  Bluefruit.Scanner.stop();

  Serial.print("connected handle=");
  Serial.println(connHandle);

  if (peerStepService.discover(connHandle) && peerStepChar.discover()) {
    g_peerStepCharReady = true;
    Serial.println("peer_step_char=ready");
  } else {
    Serial.println("peer_step_char=not_found");
  }

  g_lastWriteMs = millis();
  g_connected = true;
}

static void disconnectCallback(uint16_t connHandle, uint8_t reason) {
  (void)connHandle;
  Serial.print("disconnected reason=0x");
  if (reason < 16U) {
    Serial.print('0');
  }
  Serial.println(reason, HEX);

  g_connected = false;
  g_connHandle = BLE_CONN_HANDLE_INVALID;
  g_peerStepCharReady = false;
  g_scanRestartPending = true;
}

static void setupLocalStepService() {
  localStepService.begin();

  localStepChar.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
  localStepChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  localStepChar.setMaxLen(kStepDataLen);
  localStepChar.setWriteCallback(localStepWriteCallback);
  localStepChar.begin();

  fillStepData(g_localSequence);
  localStepChar.write(g_localStepData, sizeof(g_localStepData));
}

static void setupScanner() {
  Bluefruit.Scanner.setRxCallback(scanCallback);
  Bluefruit.Scanner.restartOnDisconnect(false);
  Bluefruit.Scanner.useActiveScan(true);
  Bluefruit.Scanner.setIntervalMS(60, 30);
  Bluefruit.Scanner.start(0);
}

static void maybeWritePeerStepData() {
  if (!g_connected || !g_peerStepCharReady) {
    return;
  }
  const uint32_t now = millis();
  if (g_lastWriteMs != 0U && (now - g_lastWriteMs) < kWriteIntervalMs) {
    return;
  }

  fillStepData(++g_localSequence);
  localStepChar.write(g_localStepData, sizeof(g_localStepData));

  const uint16_t written =
      peerStepChar.write(g_localStepData, sizeof(g_localStepData), true);
  const uint32_t crc = fnv1a(g_localStepData, sizeof(g_localStepData));
  printBridgeStatus(written == sizeof(g_localStepData)
                        ? "zephyr_step_write=PASS"
                        : "zephyr_step_write=FAIL",
                    written, crc);
  if (written != sizeof(g_localStepData)) {
    Bluefruit.debugPrintLongWriteState(Serial);
  }
  g_lastWriteMs = now;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("BleChannelSoundingZephyrCompatInitiator");

  configureBoardPower();

  Bluefruit.configUuid128Count(8);
  Bluefruit.configPrphConn(247, 2, 1, 1);
  Bluefruit.configCentralConn(247, 2, 1, 1);
  Bluefruit.begin(1, 1);
  Bluefruit.setName("Arduino CS Initiator");
  Bluefruit.setTxPower(kTxPowerDbm);
  Bluefruit.setConnLedInterval(false);

  peerStepService.begin();
  peerStepChar.begin();
  setupLocalStepService();

  Bluefruit.Central.setConnectCallback(connectCallback);
  Bluefruit.Central.setDisconnectCallback(disconnectCallback);

  setupScanner();
  Serial.println("scan_for=CS Sample");
}

void loop() {
  if (g_scanRestartPending && !g_connected) {
    g_scanRestartPending = false;
    delay(kReconnectDelayMs);
    setupScanner();
  }

  if (g_peerStepWriteSeen) {
    g_peerStepWriteSeen = false;
    printBridgeStatus("zephyr_step_rx=PASS", g_peerWriteBytes, g_peerWriteCrc);
  }

  maybeWritePeerStepData();
  delay(5);
}
