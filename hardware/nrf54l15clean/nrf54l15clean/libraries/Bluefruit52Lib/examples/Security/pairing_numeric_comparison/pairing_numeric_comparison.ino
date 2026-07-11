#include <Arduino.h>
#include <bluefruit.h>
#include <string.h>

// LE Secure Connections Numeric Comparison peripheral.
// Pair from a phone, compare the six digits printed by both devices, then
// enter "yes" or "no" in Serial Monitor. The reply is asynchronous so BLE
// connection events continue while the user makes the decision.

BLEUart bleuart;

namespace {

bool comparisonPending = false;
uint8_t pendingPasskey[6] = {0};

void printPasskey(const uint8_t passkey[6]) {
  for (size_t i = 0; i < 6U; ++i) {
    Serial.write(passkey[i]);
  }
}

void startAdvertising() {
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.Advertising.addName();
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

void connectCallback(uint16_t connHandle) {
  BLEConnection* connection = Bluefruit.Connection(connHandle);
  Serial.println("Connected; requesting Numeric Comparison");
  if (connection != nullptr && !connection->requestPairing()) {
    Serial.println("Pairing request was not queued");
  }
}

void disconnectCallback(uint16_t, uint8_t reason) {
  comparisonPending = false;
  Serial.print("Disconnected reason=0x");
  Serial.println(reason, HEX);
}

void securedCallback(uint16_t connHandle) {
  Serial.print("Encrypted authenticated=");
  Serial.println(Bluefruit.Security.isAuthenticated(connHandle) ? "yes" : "no");
}

void pairCompleteCallback(uint16_t, uint8_t status) {
  Serial.print("Pair complete status=0x");
  Serial.println(status, HEX);
}

void pollComparisonRequest() {
  uint8_t passkey[6] = {0};
  bool matchRequest = false;
  if (!Bluefruit.Security.getPendingPairingPasskey(passkey, &matchRequest) ||
      !matchRequest) {
    return;
  }
  if (comparisonPending &&
      memcmp(passkey, pendingPasskey, sizeof(pendingPasskey)) == 0) {
    return;
  }

  memcpy(pendingPasskey, passkey, sizeof(pendingPasskey));
  comparisonPending = true;
  Serial.print("Compare: ");
  printPasskey(passkey);
  Serial.println("  Reply with: yes or no");
}

void pollSerialReply() {
  static char line[8] = {0};
  static size_t length = 0U;

  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') {
      continue;
    }
    if (ch != '\n') {
      if (length + 1U < sizeof(line)) {
        line[length++] = ch;
      }
      continue;
    }

    line[length] = '\0';
    if (comparisonPending &&
        (strcmp(line, "yes") == 0 || strcmp(line, "no") == 0)) {
      const bool accept = strcmp(line, "yes") == 0;
      if (Bluefruit.Security.replyPendingPairingPasskey(accept)) {
        comparisonPending = false;
        Serial.println(accept ? "Numeric Comparison accepted"
                              : "Numeric Comparison rejected");
      }
    }
    length = 0U;
    line[0] = '\0';
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t startedMs = millis();
  while (!Serial && (millis() - startedMs) < 1500UL) {
    delay(10);
  }

  Bluefruit.autoConnLed(false);
  Bluefruit.begin(1, 0);
  Bluefruit.setName("X54-NUMCMP");
  Bluefruit.setTxPower(0);

  Bluefruit.Security.setIOCaps(true, true, false);
  Bluefruit.Security.setSecuredCallback(securedCallback);
  Bluefruit.Security.setPairCompleteCallback(pairCompleteCallback);
  Bluefruit.Periph.setConnectCallback(connectCallback);
  Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

  bleuart.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
  bleuart.begin();
  startAdvertising();
  Serial.println("Advertising as X54-NUMCMP");
}

void loop() {
  pollComparisonRequest();
  pollSerialReply();

  while (bleuart.available() > 0) {
    const int ch = bleuart.read();
    if (ch >= 0) {
      Serial.write(static_cast<uint8_t>(ch));
    }
  }
}
