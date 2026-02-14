#include <Arduino.h>
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin();
  delay(1000);

  Serial.println("I2C scanner start");

  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    int err = Wire.endTransmission();
    if (err == 0) {
      Serial.print("Found device @ 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.println(address, HEX);
    }
    delay(5);
  }

  Serial.println("I2C scanner done");
}

void loop() {
  delay(1000);
}
