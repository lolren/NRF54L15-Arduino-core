/*
 * WireScanner
 *
 * Scans the standard 7-bit I2C address range once every five seconds.
 * endTransmission() returns zero only after a target acknowledges a real bus
 * address phase; an empty bus must report found=0.
 */

#include <Wire.h>

void scanBus() {
  uint8_t found = 0U;
  uint8_t errors = 0U;
  for (uint8_t address = 0x01U; address <= 0x7EU; ++address) {
    Wire.beginTransmission(address);
    const uint8_t status = Wire.endTransmission();
    if (status == 0U) {
      Serial.print("I2C device at 0x");
      if (address < 0x10U) {
        Serial.print('0');
      }
      Serial.println(address, HEX);
      ++found;
    } else if (status != 2U) {
      // Status 2 is the expected address NACK for an unused address. Other
      // statuses usually indicate missing pull-ups or a stuck bus.
      ++errors;
    }
  }
  Serial.print("scan done found=");
  Serial.print(found);
  Serial.print(" errors=");
  Serial.println(errors);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin();
  Wire.setClock(100000UL);
  Serial.println("WireScanner start");
  scanBus();
}

void loop() {
  delay(5000);
  scanBus();
}
