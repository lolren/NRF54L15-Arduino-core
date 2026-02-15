#include <Arduino.h>
#include <Wire.h>

/*
  XIAO nRF54L15 board test: I2C bus scanner.

  Board I2C pins:
  - SDA: D4
  - SCL: D5
*/

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin();
  Wire.setClock(100000U);

  Serial.println("XIAO nRF54L15 I2C scanner");
  Serial.println("Scanning addresses 0x08..0x77");
}

void loop() {
  uint8_t found = 0;
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    Wire.beginTransmission(address);
    const int err = Wire.endTransmission();

    if (err == 0) {
      Serial.print("Found I2C device at 0x");
      if (address < 16) {
        Serial.print('0');
      }
      Serial.println(address, HEX);
      ++found;
    }
  }

  if (found == 0) {
    Serial.println("No I2C devices found.");
  } else {
    Serial.print("Found devices: ");
    Serial.println(found);
  }

  Serial.println("---");
  delay(2000);
}

