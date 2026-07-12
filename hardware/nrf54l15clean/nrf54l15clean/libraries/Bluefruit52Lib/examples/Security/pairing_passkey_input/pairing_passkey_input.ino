#include <bluefruit.h>

BLEUart bleuart;

static uint8_t enteredPasskey[6] = {0};
static uint8_t enteredDigits = 0U;
static bool inputRequested = false;

void startAdvertising();
void passkeyRequestCallback(uint16_t connHandle, uint8_t passkey[6]);
void pairingCompleteCallback(uint16_t connHandle, uint8_t authStatus);
void securedCallback(uint16_t connHandle);

void setup() {
  Serial.begin(115200);
  const uint32_t serialWaitStarted = millis();
  while (!Serial && millis() - serialWaitStarted < 1500UL) {
    delay(10);
  }

  Bluefruit.begin();
  Bluefruit.setName("X54-INPUT");
  Bluefruit.Security.setIOCaps(false, false, true);
  Bluefruit.Security.setMITM(true);
  Bluefruit.Security.setPairPasskeyRequestCallback(passkeyRequestCallback);
  Bluefruit.Security.setPairCompleteCallback(pairingCompleteCallback);
  Bluefruit.Security.setSecuredCallback(securedCallback);

  bleuart.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
  bleuart.begin();
  startAdvertising();

  Serial.println("Passkey-input peripheral ready");
  Serial.println("Enter the six digits displayed by the central");
}

void loop() {
  while (Serial.available()) {
    const int value = Serial.read();
    if (value >= '0' && value <= '9') {
      if (enteredDigits < sizeof(enteredPasskey)) {
        enteredPasskey[enteredDigits++] = static_cast<uint8_t>(value);
      }
    } else if (value == '\n' || value == '\r') {
      if (enteredDigits != 0U && enteredDigits != sizeof(enteredPasskey)) {
        enteredDigits = 0U;
        Serial.println("Passkey cleared; enter exactly six digits");
      }
    }
  }

  if (inputRequested && enteredDigits == sizeof(enteredPasskey)) {
    uint32_t requestId = 0UL;
    if (Bluefruit.Security.getPendingPairingPasskeyRequest(&requestId) &&
        Bluefruit.Security.replyPendingPairingPasskey(requestId,
                                                      enteredPasskey)) {
      Serial.println("Passkey submitted");
      inputRequested = false;
      enteredDigits = 0U;
      memset(enteredPasskey, 0, sizeof(enteredPasskey));
    }
  }

  while (bleuart.available()) {
    Serial.write(static_cast<uint8_t>(bleuart.read()));
  }
}

void passkeyRequestCallback(uint16_t connHandle, uint8_t passkey[6]) {
  (void)connHandle;
  if (enteredDigits == sizeof(enteredPasskey)) {
    memcpy(passkey, enteredPasskey, sizeof(enteredPasskey));
    enteredDigits = 0U;
    memset(enteredPasskey, 0, sizeof(enteredPasskey));
    Serial.println("Passkey submitted");
    return;
  }
  inputRequested = true;
  Serial.println("Passkey requested; enter six digits in Serial Monitor");
}

void pairingCompleteCallback(uint16_t connHandle, uint8_t authStatus) {
  (void)connHandle;
  Serial.println(authStatus == BLE_GAP_SEC_STATUS_SUCCESS
                     ? "Pairing succeeded"
                     : "Pairing failed");
  inputRequested = false;
  enteredDigits = 0U;
  memset(enteredPasskey, 0, sizeof(enteredPasskey));
}

void securedCallback(uint16_t connHandle) {
  BLEConnection* connection = Bluefruit.Connection(connHandle);
  Serial.print("Secured, authenticated: ");
  Serial.println(connection != nullptr && connection->authenticated() ? "yes"
                                                                       : "no");
}

void startAdvertising() {
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.Advertising.addName();
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32U, 244U);
  Bluefruit.Advertising.setFastTimeout(30U);
  Bluefruit.Advertising.start(0U);
}
