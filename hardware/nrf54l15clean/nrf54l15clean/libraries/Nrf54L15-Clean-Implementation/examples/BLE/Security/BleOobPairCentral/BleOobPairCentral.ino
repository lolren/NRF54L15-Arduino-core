#include <Arduino.h>
#include <bluefruit.h>
#include <string.h>

// Two-board LE Secure Connections OOB pairing demo, central side.
//
// Use with BLE > Security > BleOobPairPeripheral.
// Paste the peripheral's printed "peer <r> <c>" line into this serial monitor,
// then paste this board's printed "peer <r> <c>" line into the peripheral.
// The central starts scanning only after peer OOB data is configured.
//
// BLE_OOB_MODE selects which OOB payloads cross the out-of-band channel:
//   0: mutual (default)
//   1: peripheral -> central only
//   2: central -> peripheral only

#ifndef BLE_OOB_MODE
#define BLE_OOB_MODE 0
#endif

#ifndef BLE_OOB_CLEAR_BONDS_ON_BOOT
#define BLE_OOB_CLEAR_BONDS_ON_BOOT 1
#endif

#ifndef BLE_OOB_REQUEST_DLE
#define BLE_OOB_REQUEST_DLE 1
#endif

BLEClientUart clientUart;

namespace {

constexpr uint32_t kSerialWaitMs = 1500UL;
constexpr uint32_t kPingPeriodMs = 3000UL;
constexpr uint32_t kPairRequestDelayMs =
    (BLE_OOB_REQUEST_DLE != 0) ? 1500UL : 500UL;
constexpr uint8_t kOobMode = BLE_OOB_MODE;
static_assert(kOobMode <= 2U, "BLE_OOB_MODE must be 0, 1, or 2");
constexpr bool kPublishLocalOob = (kOobMode == 0U) || (kOobMode == 2U);
constexpr bool kNeedsPeerOob = (kOobMode == 0U) || (kOobMode == 1U);

uint8_t localR[16] = {0};
uint8_t localC[16] = {0};
uint8_t peerR[16] = {0};
uint8_t peerC[16] = {0};
bool peerOobReady = !kNeedsPeerOob;
bool scanningStarted = false;
bool uartReady = false;
bool uartDiscoveryPending = false;
bool pairingRequestPending = false;
uint16_t connectedHandle = BLE_CONN_HANDLE_INVALID;
uint32_t connectedAtMs = 0U;
uint32_t lastPingMs = 0;
uint32_t pingSeq = 0;

void scanCallback(ble_gap_evt_adv_report_t* report);

int hexValue(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
  if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
  return -1;
}

bool parseHex16(const char* text, uint8_t out[16]) {
  if (text == nullptr || out == nullptr || strlen(text) != 32U) return false;
  for (size_t i = 0; i < 16U; ++i) {
    const int hi = hexValue(text[i * 2U]);
    const int lo = hexValue(text[i * 2U + 1U]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

void printHex16(const uint8_t value[16]) {
  static const char kHex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < 16U; ++i) {
    Serial.write(kHex[value[i] >> 4]);
    Serial.write(kHex[value[i] & 0x0FU]);
  }
}

void printLocalOob() {
  Serial.println();
  Serial.println("BLE OOB central");
  Serial.print("OOB mode=");
  Serial.println(kOobMode == 0U ? "mutual" :
                 (kOobMode == 1U ? "peripheral-to-central"
                                  : "central-to-peripheral"));
  if (kPublishLocalOob) {
    Serial.print("local_r=");
    printHex16(localR);
    Serial.println();
    Serial.print("local_c=");
    printHex16(localC);
    Serial.println();
    Serial.print("paste_on_peer: peer ");
    printHex16(localR);
    Serial.write(' ');
    printHex16(localC);
    Serial.println();
  } else {
    Serial.println("local_oob=not-sent");
  }
  if (kNeedsPeerOob) {
    Serial.println("Waiting for peer <r> <c> from the peripheral...");
  } else {
    Serial.println("peer_oob=not-required");
  }
}

void startScan() {
  if (scanningStarted || Bluefruit.connected()) return;

  Bluefruit.Scanner.setRxCallback(scanCallback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(160, 80);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.start(0);
  scanningStarted = true;
  Serial.println("Scanning for BLE UART peripheral with OOB data ready");
}

void handlePeerLine(char* line) {
  char* cmd = strtok(line, " \t,");
  if (cmd == nullptr) return;
  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    printLocalOob();
    return;
  }
  if (!kNeedsPeerOob) {
    Serial.println("Peer OOB data is not used in this one-way mode");
    return;
  }
  if (strcmp(cmd, "peer") != 0) {
    Serial.println("Use: peer <32-hex-r> <32-hex-c>");
    return;
  }

  char* rText = strtok(nullptr, " \t,");
  char* cText = strtok(nullptr, " \t,");
  if (!parseHex16(rText, peerR) || !parseHex16(cText, peerC)) {
    Serial.println("Bad OOB data. Use exactly 32 hex chars for r and c.");
    return;
  }

  Bluefruit.Security.setOobRemoteData(peerR, peerC);
  Bluefruit.Security.setOobFlag(true);
  peerOobReady = true;
  Serial.println("Peer OOB data stored");
  startScan();
}

void pollSerialCommands() {
  static char line[96] = {0};
  static size_t used = 0;

  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      line[used] = '\0';
      handlePeerLine(line);
      used = 0;
      line[0] = '\0';
      continue;
    }
    if (used + 1U < sizeof(line)) {
      line[used++] = ch;
    }
  }
}

void scanCallback(ble_gap_evt_adv_report_t* report) {
  if (Bluefruit.Scanner.checkReportForService(report, clientUart)) {
    Serial.println("BLE UART service found, connecting");
    Bluefruit.Central.connect(report);
  } else {
    Bluefruit.Scanner.resume();
  }
}

void connectCallback(uint16_t connHandle) {
  BLEConnection* conn = Bluefruit.Connection(connHandle);
  Serial.println("Connected");
  uartReady = false;
  uartDiscoveryPending = false;
  connectedHandle = connHandle;
  connectedAtMs = millis();
  pairingRequestPending = peerOobReady && conn != nullptr;
  if (conn != nullptr && BLE_OOB_REQUEST_DLE != 0) {
    Serial.print("Requesting data length update: ");
    Serial.println(conn->requestDataLengthUpdate() ? "queued" : "failed");
  }
}

void disconnectCallback(uint16_t connHandle, uint8_t reason) {
  (void)connHandle;
  uartReady = false;
  uartDiscoveryPending = false;
  pairingRequestPending = false;
  connectedHandle = BLE_CONN_HANDLE_INVALID;
  scanningStarted = false;
  Serial.print("Disconnected reason=0x");
  Serial.println(reason, HEX);
  if (peerOobReady) startScan();
}

void securedCallback(uint16_t connHandle) {
  Serial.println("Connection encrypted with OOB pairing");
  Serial.print("OOB mutually authenticated=");
  Serial.println(Bluefruit.Security.isAuthenticated(connHandle) ? "yes" : "no");
}

void pairCompleteCallback(uint16_t connHandle, uint8_t status) {
  Serial.print("Pair complete status=0x");
  Serial.println(status, HEX);
  if (status == BLE_GAP_SEC_STATUS_SUCCESS) {
    connectedHandle = connHandle;
    uartDiscoveryPending = true;
  }
}

void clientUartRxCallback(BLEClientUart& uart) {
  while (uart.available() > 0) {
    const int ch = uart.read();
    if (ch >= 0) Serial.write(static_cast<uint8_t>(ch));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t startMs = millis();
  while (!Serial && millis() - startMs < kSerialWaitMs) {
    delay(10);
  }

  Bluefruit.autoConnLed(false);
  Bluefruit.begin(0, 1);
  if (BLE_OOB_CLEAR_BONDS_ON_BOOT != 0) {
    Bluefruit.Central.clearBonds();
  }
  Bluefruit.setTxPower(0);

  Bluefruit.Security.setIOCaps(false, false, false);
  Bluefruit.Security.setPairCompleteCallback(pairCompleteCallback);
  Bluefruit.Security.setSecuredCallback(securedCallback);
  if (kPublishLocalOob) {
    if (!Bluefruit.Security.generateOobData(localR, localC)) {
      Serial.println("ERROR: failed to generate local OOB data");
      while (true) delay(1000);
    }
    Bluefruit.Security.setOobFlag(true);
  }

  clientUart.begin();
  clientUart.setRxCallback(clientUartRxCallback);

  Bluefruit.Central.setConnectCallback(connectCallback);
  Bluefruit.Central.setDisconnectCallback(disconnectCallback);

  printLocalOob();
  if (!kNeedsPeerOob) startScan();
}

void loop() {
  pollSerialCommands();

  if (pairingRequestPending && Bluefruit.connected() &&
      (millis() - connectedAtMs >= kPairRequestDelayMs)) {
    BLEConnection* conn = Bluefruit.Connection(connectedHandle);
    pairingRequestPending = false;
    Serial.println("Requesting OOB pairing");
    if (conn == nullptr || !conn->requestPairing()) {
      Serial.println("ERROR: OOB pairing request was not queued");
    }
  }

  if (uartDiscoveryPending && Bluefruit.connected()) {
    uartDiscoveryPending = false;
    if (clientUart.discover(connectedHandle)) {
      clientUart.enableTXD();
      uartReady = true;
      Serial.println("BLE UART discovered");
    } else {
      Serial.println("BLE UART discovery failed");
    }
  }

  if (uartReady && Bluefruit.connected() &&
      (millis() - lastPingMs >= kPingPeriodMs)) {
    lastPingMs = millis();
    clientUart.print("oob central ");
    clientUart.println(pingSeq++);
  }
}
