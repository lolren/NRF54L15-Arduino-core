#include <Bluetooth.h>

#define SCAN_FILTER_NAME ""
#define SCAN_FILTER_ADDRESS ""

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("BLE Scan Example (Clean Core)");
  BLE.begin("XIAO-nRF54L15");

  if (SCAN_FILTER_NAME[0] != '\0') {
    if (!BLE.setScanFilterName(SCAN_FILTER_NAME)) {
      Serial.print("Failed to set name filter, err=");
      Serial.println(BLE.lastError());
    }
  } else {
    BLE.clearScanFilterName();
  }

  if (SCAN_FILTER_ADDRESS[0] != '\0') {
    if (!BLE.setScanFilterAddress(SCAN_FILTER_ADDRESS)) {
      Serial.print("Failed to set address filter, err=");
      Serial.println(BLE.lastError());
    }
  } else {
    BLE.clearScanFilterAddress();
  }

  if (BLE.scanFilterEnabled()) {
    if (BLE.scanFilterName().length() > 0) {
      Serial.print("Scan filter name: ");
      Serial.println(BLE.scanFilterName());
    }
    if (BLE.scanFilterAddress().length() > 0) {
      Serial.print("Scan filter address: ");
      Serial.println(BLE.scanFilterAddress());
    }
  }
}

void loop()
{
  if (BLE.scan(3000)) {
    digitalWrite(LED_BUILTIN, LOW);
    Serial.print("Address: ");
    Serial.print(BLE.address());
    Serial.print(" Name: ");
    Serial.print(BLE.name());
    Serial.print(" RSSI: ");
    Serial.println(BLE.rssi());
    delay(200);
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    int err = BLE.lastError();
    if (err == BluetoothClass::ErrorScanFilterNoMatch) {
      Serial.println("No device matched the active scan filter.");
    } else if (err == -123) {
      Serial.print("BLE scan failed, err=");
      Serial.print(err);
      Serial.print(" -> ");
      Serial.println("BLE disabled by Tools > Radio Profile");
      Serial.println("Set Tools > Radio Profile to BLE Only (or BLE + 802.15.4).");
    } else if (err == -88) {
      Serial.print("BLE scan failed, err=");
      Serial.print(err);
      Serial.print(" -> ");
      Serial.println("BLE stack not linked in clean no-blob core yet.");
      Serial.println("Use the Zephyr core board for working BLE right now.");
    } else {
      Serial.print("BLE scan failed, err=");
      Serial.print(err);
      Serial.print(" -> ");
      Serial.println("BLE unavailable.");
    }
    delay(2000);
  }
}
