#include <bluefruit.h>

namespace {

constexpr uint16_t kGenericAccessService = 0x1800U;
constexpr uint16_t kDeviceNameCharacteristic = 0x2A00U;
constexpr uint16_t kAppearanceCharacteristic = 0x2A01U;

BLEClientService genericAccess(kGenericAccessService);
BLEClientCharacteristic deviceName(kDeviceNameCharacteristic);
BLEClientCharacteristic appearance(kAppearanceCharacteristic);

void scanCallback(ble_gap_evt_adv_report_t* report) {
  if (!Bluefruit.Central.connect(report)) {
    Bluefruit.Scanner.resume();
  }
}

void disconnectCallback(uint16_t connHandle, uint8_t reason) {
  (void)connHandle;
  Serial.print("Disconnected: 0x");
  Serial.println(reason, HEX);
}

void connectCallback(uint16_t connHandle) {
  if (!genericAccess.discover(connHandle)) {
    Serial.println("Generic Access service not found");
    Bluefruit.disconnect(connHandle);
    return;
  }

  Bluefruit.Discovery.setHandleRange(genericAccess.getHandleRange());
  const uint8_t found = Bluefruit.Discovery.discoverCharacteristic(
      connHandle, deviceName, appearance);
  Serial.print("Characteristics found: ");
  Serial.println(found);

  if (deviceName.discovered()) {
    char name[33] = {};
    const uint16_t length = deviceName.read(name, sizeof(name) - 1U);
    name[length] = '\0';
    Serial.print("Device name: ");
    Serial.println(name);
  }

  // readCharByUuid is useful when a persistent client object is unnecessary.
  uint16_t peerAppearance = 0U;
  const ble_gattc_handle_range_t range = genericAccess.getHandleRange();
  if (Bluefruit.Gatt.readCharByUuid(
          connHandle, BLEUuid(kAppearanceCharacteristic), &peerAppearance,
          sizeof(peerAppearance), range.start_handle, range.end_handle) ==
      sizeof(peerAppearance)) {
    Serial.print("Appearance: 0x");
    Serial.println(peerAppearance, HEX);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  if (!Bluefruit.begin(0U, 1U)) {
    Serial.println("Bluefruit initialization failed");
    return;
  }

  genericAccess.begin();
  deviceName.begin(&genericAccess);
  appearance.begin(&genericAccess);

  Bluefruit.Central.setConnectCallback(connectCallback);
  Bluefruit.Central.setDisconnectCallback(disconnectCallback);
  Bluefruit.Scanner.setRxCallback(scanCallback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(160U, 80U);
  Bluefruit.Scanner.useActiveScan(true);
  Bluefruit.Scanner.start(0U);
}

void loop() { yield(); }
