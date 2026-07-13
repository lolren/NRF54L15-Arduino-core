#include <bluefruit.h>

BLEService testService(0x1815);
BLECharacteristic testValue(0x2A56);
#ifndef ROBUST_CACHE_SCHEMA_REVISION
#define ROBUST_CACHE_SCHEMA_REVISION 1
#endif
#if ROBUST_CACHE_SCHEMA_REVISION >= 2
BLECharacteristic revisionValue(0x2A57);
#endif
#if ROBUST_CACHE_SCHEMA_REVISION >= 3
BLECharacteristic revisionMarker(0x2A58);
#endif
#if ROBUST_CACHE_SCHEMA_REVISION >= 4
BLECharacteristic revisionSentinel(0x2A59);
#endif

void printDatabaseHash() {
  uint8_t hash[16] = {0};
  if (!Bluefruit.Gatt.databaseHash(hash)) {
    Serial.println("Database Hash unavailable");
    return;
  }
  Serial.print("Database Hash: ");
  for (uint8_t octet : hash) {
    if (octet < 0x10U) {
      Serial.print('0');
    }
    Serial.print(octet, HEX);
  }
  Serial.println();
}

void connectCallback(uint16_t connHandle) {
  Serial.println("Connected");
  BLEConnection* connection = Bluefruit.Connection(connHandle);
  if (connection != nullptr && !connection->bonded()) {
    connection->requestPairing();
  }
}

void disconnectCallback(uint16_t, uint8_t reason) {
  Serial.print("Disconnected: 0x");
  Serial.println(reason, HEX);
}

void pairingCompleteCallback(uint16_t, uint8_t authStatus) {
  Serial.print("Pairing status: 0x");
  Serial.println(authStatus, HEX);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Bluefruit.configServiceChanged(true);
  Bluefruit.Security.setIOCaps(false, false, false);
  Bluefruit.Security.setPairCompleteCallback(pairingCompleteCallback);
  Bluefruit.Periph.setConnectCallback(connectCallback);
  Bluefruit.Periph.setDisconnectCallback(disconnectCallback);
  Bluefruit.begin();
  Bluefruit.setName("nRF54 Robust Cache");

  testService.begin();
  testValue.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  testValue.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  testValue.setFixedLen(1);
  testValue.begin();
  testValue.write8(0);
#if ROBUST_CACHE_SCHEMA_REVISION >= 2
  revisionValue.setProperties(CHR_PROPS_READ);
  revisionValue.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  revisionValue.setFixedLen(1);
  revisionValue.begin();
  revisionValue.write8(ROBUST_CACHE_SCHEMA_REVISION);
#endif
#if ROBUST_CACHE_SCHEMA_REVISION >= 3
  revisionMarker.setProperties(CHR_PROPS_READ);
  revisionMarker.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  revisionMarker.setFixedLen(1);
  revisionMarker.begin();
  revisionMarker.write8(ROBUST_CACHE_SCHEMA_REVISION);
#endif
#if ROBUST_CACHE_SCHEMA_REVISION >= 4
  revisionSentinel.setProperties(CHR_PROPS_READ);
  revisionSentinel.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  revisionSentinel.setFixedLen(1);
  revisionSentinel.begin();
  revisionSentinel.write8(ROBUST_CACHE_SCHEMA_REVISION);
#endif

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(testService);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);

  printDatabaseHash();
}

void loop() {
  static uint32_t lastStatus = 0;
  static uint8_t value = 0;
  if (Bluefruit.connected() && millis() - lastStatus >= 2000U) {
    lastStatus = millis();
    Serial.print("Client features: 0x");
    Serial.print(Bluefruit.Gatt.clientSupportedFeatures(), HEX);
    Serial.print(" change-aware: ");
    Serial.println(Bluefruit.Gatt.clientChangeAware() ? "yes" : "no");
    testValue.write8(++value);
    testValue.notify8(value);
  }
}
