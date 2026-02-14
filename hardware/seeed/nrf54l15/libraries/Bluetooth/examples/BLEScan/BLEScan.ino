#include <Arduino.h>
#include <Bluetooth.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  delay(1500);

  if (!BLE.begin("XIAO-nRF54L15-Scanner")) {
    Serial.println("BLE init failed");
  } else {
    Serial.println("BLE init ok");
  }
}

void loop() {
  Serial.println("BLE scan start...");

  if (BLE.scan(5000)) {
    Serial.print("Device: ");
    Serial.print(BLE.name());
    Serial.print("  Address: ");
    Serial.print(BLE.address());
    Serial.print("  RSSI: ");
    Serial.println(BLE.rssi());
    digitalWrite(LED_BUILTIN, LOW);  // active-low LED on
  } else {
    Serial.print("No scan result, err=");
    Serial.println(BLE.lastError());
    digitalWrite(LED_BUILTIN, HIGH); // active-low LED off
  }

  delay(1000);
}
