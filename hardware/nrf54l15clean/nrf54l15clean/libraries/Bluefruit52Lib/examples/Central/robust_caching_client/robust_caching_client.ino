#include <bluefruit.h>

namespace {

constexpr uint16_t kGattService = 0x1801U;
constexpr uint16_t kClientSupportedFeatures = 0x2B29U;
constexpr uint16_t kDatabaseHash = 0x2B2AU;

BLEClientService gattService(kGattService);
BLEClientCharacteristic clientFeatures(kClientSupportedFeatures);
BLEClientCharacteristic databaseHash(kDatabaseHash);
bool firstWriteRejected = false;
bool robustCachingReady = false;
uint8_t lastDatabaseHash[16] = {0};

void printHash(const uint8_t hash[16]) {
  Serial.print("Database Hash: ");
  for (uint8_t i = 0U; i < 16U; ++i) {
    if (hash[i] < 0x10U) {
      Serial.print('0');
    }
    Serial.print(hash[i], HEX);
  }
  Serial.println();
}

void scanCallback(ble_gap_evt_adv_report_t* report) {
  if (!Bluefruit.Central.connect(report)) {
    Bluefruit.Scanner.resume();
  }
}

void connectCallback(uint16_t connHandle) {
  if (!gattService.discover(connHandle)) {
    Serial.println("GATT service not found");
    Bluefruit.disconnect(connHandle);
    return;
  }

  Bluefruit.Discovery.setHandleRange(gattService.getHandleRange());
  const uint8_t found = Bluefruit.Discovery.discoverCharacteristic(
      connHandle, clientFeatures, databaseHash);
  if (found != 2U || !clientFeatures.discovered() ||
      !databaseHash.discovered()) {
    Serial.println("Robust Caching characteristics not found");
    Bluefruit.disconnect(connHandle);
    return;
  }

  const uint8_t robustCaching = 0x01U;
  if (!clientFeatures.write(&robustCaching, sizeof(robustCaching))) {
    firstWriteRejected = true;
    Serial.println("First CSF write rejected; retrying after cache resync");
    if (!clientFeatures.write(&robustCaching, sizeof(robustCaching))) {
      Serial.println("Client Supported Features retry failed");
      return;
    }
  }

  uint8_t hash[16] = {0};
  if (databaseHash.read(hash, sizeof(hash)) != sizeof(hash)) {
    Serial.println("Database Hash read failed");
    return;
  }
  Serial.println("Robust Caching enabled");
  printHash(hash);
  memcpy(lastDatabaseHash, hash, sizeof(lastDatabaseHash));
  robustCachingReady = true;

}

void disconnectCallback(uint16_t, uint8_t reason) {
  Serial.print("Disconnected: 0x");
  Serial.println(reason, HEX);
}

void pairingCompleteCallback(uint16_t, uint8_t authStatus) {
  Serial.print("Pairing status: 0x");
  Serial.println(authStatus, HEX);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  if (!Bluefruit.begin(0U, 1U)) {
    Serial.println("Bluefruit initialization failed");
    return;
  }

  Bluefruit.Security.setIOCaps(false, false, false);
  Bluefruit.Security.setPairCompleteCallback(pairingCompleteCallback);

  gattService.begin();
  clientFeatures.begin(&gattService);
  databaseHash.begin(&gattService);

  Bluefruit.Central.setConnectCallback(connectCallback);
  Bluefruit.Central.setDisconnectCallback(disconnectCallback);
  Bluefruit.Scanner.setRxCallback(scanCallback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(160U, 80U);
  Bluefruit.Scanner.useActiveScan(true);
  Bluefruit.Scanner.start(0U);
}

void loop() {
  static uint32_t lastStatus = 0U;
  if (millis() - lastStatus >= 2000U) {
    lastStatus = millis();
    Serial.print("Robust cache ready: ");
    Serial.print(robustCachingReady ? "yes" : "no");
    Serial.print(" first write rejected: ");
    Serial.println(firstWriteRejected ? "yes" : "no");
    if (robustCachingReady) {
      printHash(lastDatabaseHash);
    }
  }
  yield();
}
