/*
  Bluefruit indication central

  Flash Peripheral > indication_peripheral on another board. This central
  discovers the counter, enables indications, and verifies that indication
  traffic uses the distinct indication callback.
*/

#include <bluefruit.h>

static constexpr uint16_t kServiceUuid = 0xFAD0U;
static constexpr uint16_t kCounterUuid = 0xFAD1U;

BLEClientService indicationService(kServiceUuid);
BLEClientCharacteristic indicationCounter(kCounterUuid);

static volatile uint32_t g_indicationCount = 0UL;
static volatile uint32_t g_unexpectedNotificationCount = 0UL;

static void indicationCallback(BLEClientCharacteristic* characteristic,
                               uint8_t* data, uint16_t len) {
  if (characteristic != &indicationCounter || data == nullptr ||
      len != sizeof(uint32_t)) {
    Serial.println("invalid indication");
    return;
  }
  uint32_t value = 0UL;
  memcpy(&value, data, sizeof(value));
  ++g_indicationCount;
  Serial.print("indication=");
  Serial.print(value);
  Serial.print(" count=");
  Serial.println(g_indicationCount);
}

static void unexpectedNotificationCallback(
    BLEClientCharacteristic* characteristic, uint8_t* data, uint16_t len) {
  (void)characteristic;
  (void)data;
  (void)len;
  ++g_unexpectedNotificationCount;
  Serial.println("unexpected notification callback");
}

static void scanCallback(ble_gap_evt_adv_report_t* report) {
  (void)Bluefruit.Central.connect(report);
}

static void connectCallback(uint16_t connHandle) {
  Serial.println("connected; discovering indication service");
  if (!indicationService.discover(connHandle) ||
      !indicationCounter.discover()) {
    Serial.println("indication service not found");
    Bluefruit.disconnect(connHandle);
    return;
  }

  Serial.print("value_handle=0x");
  Serial.print(indicationCounter.valueHandle(), HEX);
  Serial.print(" properties=0x");
  Serial.print(indicationCounter.properties(), HEX);
  Serial.print(" initial=");
  Serial.println(indicationCounter.read32());

  if (!indicationCounter.enableIndicate()) {
    Serial.println("could not enable indications");
    Bluefruit.disconnect(connHandle);
    return;
  }
  Serial.println("indications enabled");
}

static void disconnectCallback(uint16_t connHandle, uint8_t reason) {
  (void)connHandle;
  Serial.print("disconnected reason=0x");
  Serial.println(reason, HEX);
}

void setup() {
  Serial.begin(115200);
  const uint32_t serialStart = millis();
  while (!Serial && (millis() - serialStart) < 1500UL) {
    delay(10);
  }

  Bluefruit.begin(0U, 1U);
  Bluefruit.setName("X54-IND-CENTRAL");

  indicationService.begin();
  indicationCounter.setNotifyCallback(unexpectedNotificationCallback);
  indicationCounter.setIndicateCallback(indicationCallback);
  indicationCounter.begin();

  Bluefruit.Central.setConnectCallback(connectCallback);
  Bluefruit.Central.setDisconnectCallback(disconnectCallback);
  Bluefruit.Scanner.setRxCallback(scanCallback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(160U, 80U);
  Bluefruit.Scanner.filterUuid(indicationService.uuid);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.start(0U);
  Serial.println("Scanning for X54 indication peripheral");
}

void loop() {
  delay(10);
}
