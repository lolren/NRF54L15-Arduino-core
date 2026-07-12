/*
  Bluefruit indication peripheral

  Flash this sketch on one board and Central > central_indication on the
  other. The peripheral publishes a confirmed 32-bit counter once per second.
*/

#include <bluefruit.h>

static constexpr uint16_t kServiceUuid = 0xFAD0U;
static constexpr uint16_t kCounterUuid = 0xFAD1U;
static constexpr uint32_t kIndicationPeriodMs = 1000UL;

BLEService indicationService(kServiceUuid);
BLECharacteristic indicationCounter(kCounterUuid);

static uint32_t g_counter = 0UL;
static uint32_t g_nextIndicationMs = 0UL;

static void cccdCallback(uint16_t connHandle, BLECharacteristic* characteristic,
                         uint16_t cccdValue) {
  (void)connHandle;
  if (characteristic != &indicationCounter) {
    return;
  }
  Serial.print("CCCD=0x");
  Serial.println(cccdValue, HEX);
}

static void startAdvertising() {
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.addFlags(
      BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(indicationService);
  Bluefruit.Advertising.addName();
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32U, 244U);
  Bluefruit.Advertising.setFastTimeout(30U);
  Bluefruit.Advertising.start(0U);
}

void setup() {
  Serial.begin(115200);
  const uint32_t serialStart = millis();
  while (!Serial && (millis() - serialStart) < 1500UL) {
    delay(10);
  }

  Bluefruit.begin(1U, 0U);
  Bluefruit.setName("X54-INDICATE");

  indicationService.begin();
  indicationCounter.setProperties(CHR_PROPS_READ | CHR_PROPS_INDICATE);
  indicationCounter.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  indicationCounter.setFixedLen(
      static_cast<uint16_t>(sizeof(g_counter)));
  indicationCounter.setCccdWriteCallback(cccdCallback);
  indicationCounter.begin();
  indicationCounter.write32(g_counter);

  startAdvertising();
  Serial.println("Indication peripheral ready");
}

void loop() {
  const uint32_t now = millis();
  if (!Bluefruit.connected() || !indicationCounter.indicateEnabled() ||
      static_cast<int32_t>(now - g_nextIndicationMs) < 0) {
    delay(5);
    return;
  }

  g_nextIndicationMs = now + kIndicationPeriodMs;
  const uint32_t next = g_counter + 1UL;
  if (indicationCounter.indicate32(next)) {
    g_counter = next;
    Serial.print("indicated=");
    Serial.println(g_counter);
  }
}
